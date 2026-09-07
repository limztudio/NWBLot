// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_caustics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansGraphOwnedSoftwareCausticsEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_world_position"),
        "Software Caustics World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_depth"),
        "Software Caustics Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_shadow_visibility"),
        "Software Caustics Shadow Visibility",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticAccumulator = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_accumulator"),
        "Software Caustics Accumulator",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticHistory = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_history"),
        "Software Caustics History",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticResolveHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_resolve_half"),
        "Software Caustics Resolve Half",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticResolveGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_resolve_geometry"),
        "Software Caustics Resolve Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticIrradiance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_irradiance"),
        "Software Caustics Irradiance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_bindless_slots"),
        "Software Caustics Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_material_context_slots"),
        "Software Caustics Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticEmissionTargets = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_emission_targets"),
        "Software Caustics Emission Targets",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId meshView = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_view"),
        "Software Caustics Mesh View",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneBvhNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_scene_bvh_nodes"),
        "Software Caustics Scene BVH Nodes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_scene_instances"),
        "Software Caustics Scene Instances",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_instance_materials"),
        "Software Caustics Shadow Instance Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowMaterialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_material_typed"),
        "Software Caustics Shadow Typed Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_instances"),
        "Software Caustics Shadow Instances",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_nodes"),
        "Software Caustics Mesh Nodes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshPositions = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_positions"),
        "Software Caustics Mesh Positions",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshIndices = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_indices"),
        "Software Caustics Mesh Indices",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshAttributes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_attributes"),
        "Software Caustics Mesh Attributes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_scene_shading"),
        "Software Caustics Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_lights"),
        "Software Caustics Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(shadowVisibility.valid());
    ASSERT_TRUE(causticAccumulator.valid());
    ASSERT_TRUE(causticHistory.valid());
    ASSERT_TRUE(causticResolveHalf.valid());
    ASSERT_TRUE(causticResolveGeometry.valid());
    ASSERT_TRUE(causticIrradiance.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(causticEmissionTargets.valid());
    ASSERT_TRUE(meshView.valid());
    ASSERT_TRUE(sceneBvhNodes.valid());
    ASSERT_TRUE(sceneInstances.valid());
    ASSERT_TRUE(shadowInstanceMaterials.valid());
    ASSERT_TRUE(shadowMaterialTyped.valid());
    ASSERT_TRUE(shadowInstances.valid());
    ASSERT_TRUE(softwareMeshNodes.valid());
    ASSERT_TRUE(softwareMeshPositions.valid());
    ASSERT_TRUE(softwareMeshIndices.valid());
    ASSERT_TRUE(softwareMeshAttributes.valid());
    ASSERT_TRUE(sceneShading.valid());
    ASSERT_TRUE(lights.valid());

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
    const Graphics::GpuQueueRequest computeTransferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = causticEmissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneBvhNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = shadowInstanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = shadowMaterialTyped, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = shadowInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = softwareMeshNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = softwareMeshPositions, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = softwareMeshIndices, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = softwareMeshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/software_caustics_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::DepthWrite, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/software_caustics_prefix"))
        .setMarkerLabel("G-Buffer Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneBvhNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowMaterialTyped, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshPositions, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshIndices, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowVisibility, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/software_caustics_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibilityTask = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibilityTask.valid());

    // The no-producer black result starts the Software Caustics packet; the producer merges with it below.
    Graphics::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse irradianceClearUses[] = {
        { .resource = causticIrradiance, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics_irradiance_clear"))
        .setMarkerLabel("Software Caustics Irradiance Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(irradianceClearScheduling)
        .setDependencies(&shadowVisibilityTask, 1u)
        .setResourceUses(irradianceClearUses, LengthOf(irradianceClearUses))
    ;
    const Graphics::GpuTaskId irradianceClearTask = graph.addTask(irradianceClearDesc);
    ASSERT_TRUE(irradianceClearTask.valid());

    // The bootstrap clear follows the no-producer irradiance clear, then merges into the same Software Caustics
    // packet as the callback. Its CopyDest write must hand off to the producer's UAV accumulator access.
    const Graphics::GpuTaskResourceUse accumulatorBootstrapClearUses[] = {
        { .resource = causticAccumulator, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling = irradianceClearScheduling;
    accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc accumulatorBootstrapClearDesc;
    accumulatorBootstrapClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics_accumulator_bootstrap_clear"))
        .setMarkerLabel("Software Caustics Accumulator Bootstrap Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(accumulatorBootstrapClearScheduling)
        .setDependencies(&irradianceClearTask, 1u)
        .setResourceUses(accumulatorBootstrapClearUses, LengthOf(accumulatorBootstrapClearUses))
    ;
    const Graphics::GpuTaskId accumulatorBootstrapClearTask = graph.addTask(accumulatorBootstrapClearDesc);
    ASSERT_TRUE(accumulatorBootstrapClearTask.valid());

    const Graphics::GpuGraphResourceId softwareTraceGeometryMembers[] = {
        softwareMeshNodes,
        softwareMeshPositions,
        softwareMeshIndices,
        softwareMeshAttributes,
    };
    const Graphics::GpuGraphResourceSetId softwareTraceGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/software_caustics_trace_geometry"))
            .setMarkerLabel("Software Caustics Trace Geometry")
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
    const Graphics::GpuTaskResourceUse softwareCausticsUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = causticEmissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneBvhNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowMaterialTyped, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = causticAccumulator, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticHistory, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticResolveHalf, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticResolveGeometry, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticIrradiance, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint causticsScheduling = boundaryScheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc softwareCausticsDesc;
    softwareCausticsDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(computeRequest)
        .setScheduling(causticsScheduling)
        .setDependencies(&accumulatorBootstrapClearTask, 1u)
        .setResourceUses(softwareCausticsUses, LengthOf(softwareCausticsUses))
        .setResourceSetUses(softwareTraceGeometrySetUses, LengthOf(softwareTraceGeometrySetUses))
    ;
    const Graphics::GpuTaskId softwareCausticsTask = graph.addTask(softwareCausticsDesc);
    ASSERT_TRUE(softwareCausticsTask.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        { .resource = causticIrradiance, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&softwareCausticsTask, 1u)
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
        softwareCausticsTask,
        lighting,
        causticIrradiance,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        irradianceClearTask,
        softwareCausticsTask,
        causticIrradiance,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        accumulatorBootstrapClearTask,
        softwareCausticsTask,
        causticAccumulator,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const clearAssignment = assignments.find(irradianceClearTask);
    ASSERT_NE(clearAssignment, nullptr);
    EXPECT_EQ(clearAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledPlan.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId shadowPacket = compiledPlan.packetForTask(shadowVisibilityTask);
    const Graphics::GpuSubmissionPacketId irradianceClearPacket = compiledPlan.packetForTask(irradianceClearTask);
    const Graphics::GpuSubmissionPacketId accumulatorBootstrapClearPacket =
        compiledPlan.packetForTask(accumulatorBootstrapClearTask);
    const Graphics::GpuSubmissionPacketId causticsPacket = compiledPlan.packetForTask(softwareCausticsTask);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(irradianceClearPacket.valid());
    ASSERT_TRUE(accumulatorBootstrapClearPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_NE(shadowPreparePacket, prefixPacket);
    EXPECT_NE(prefixPacket, shadowPacket);
    EXPECT_NE(shadowPacket, causticsPacket);
    EXPECT_EQ(irradianceClearPacket, causticsPacket);
    EXPECT_EQ(accumulatorBootstrapClearPacket, causticsPacket);
    EXPECT_NE(causticsPacket, lightingPacket);
    EXPECT_EQ(compiledPlan.packetCount(), 5u);

    const Graphics::GpuCompiledTask* const compiledShadow = compiledPlan.findTask(shadowVisibilityTask).plan;
    const Graphics::GpuCompiledTask* const compiledCaustics = compiledPlan.findTask(softwareCausticsTask).plan;
    const Graphics::GpuCompiledTask* const compiledLighting = compiledPlan.findTask(lighting).plan;
    ASSERT_NE(compiledShadow, nullptr);
    ASSERT_NE(compiledCaustics, nullptr);
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuCompiledBarrier* const shadowBarriers = compiledPlan.findTask(shadowVisibilityTask).prologueBarriers;
    ASSERT_NE(shadowBarriers, nullptr);
    bool shadowTransitionsDepthToShaderResource = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledShadow->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = shadowBarriers[barrierIndex];
        shadowTransitionsDepthToShaderResource = shadowTransitionsDepthToShaderResource || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == depth
            && barrier.before == Graphics::ResourceStates::DepthWrite
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(shadowTransitionsDepthToShaderResource);

    const Graphics::GpuPacketStateSeed* const causticsSeeds = compiledPlan.findTask(softwareCausticsTask).prologueStateSeeds;
    ASSERT_NE(causticsSeeds, nullptr);
    const auto hasCausticsSeed = [&](const Graphics::GpuGraphResourceId resource, const Graphics::GpuSubmissionPacketId sourcePacket){
        for(usize seedIndex = 0u; seedIndex < compiledCaustics->prologueStateSeedCount; ++seedIndex){
            if(causticsSeeds[seedIndex].resource == resource && causticsSeeds[seedIndex].sourcePacket == sourcePacket)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasCausticsSeed(causticEmissionTargets, shadowPreparePacket));
    EXPECT_TRUE(hasCausticsSeed(meshView, prefixPacket));
    EXPECT_TRUE(hasCausticsSeed(worldPosition, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(depth, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(currentBindlessSlots, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(materialContextSlots, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(sceneBvhNodes, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(sceneInstances, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(shadowInstanceMaterials, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(shadowMaterialTyped, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(shadowInstances, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshNodes, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshPositions, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshIndices, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshAttributes, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(sceneShading, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(lights, shadowPacket));

    const auto causticsPacketWaitsFor = [&](const Graphics::GpuSubmissionPacketId producer){
        const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(causticsPacket).plan;
        const Graphics::GpuPacketDependency* const dependencies = compiledPlan.packet(causticsPacket).dependencies;
        if(packet.dependencyCount != 0u && !dependencies)
            return false;
        for(usize dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            if(dependencies[dependencyIndex].producer == producer)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(causticsPacketWaitsFor(shadowPreparePacket));
    EXPECT_TRUE(causticsPacketWaitsFor(prefixPacket));
    EXPECT_TRUE(causticsPacketWaitsFor(shadowPacket));

    const Graphics::GpuCompiledBarrier* const causticsBarriers = compiledPlan.findTask(softwareCausticsTask).prologueBarriers;
    ASSERT_NE(causticsBarriers, nullptr);
    const auto hasCausticsBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledCaustics->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = causticsBarriers[barrierIndex];
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
    EXPECT_FALSE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        depth,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::DepthRead
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticAccumulator,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticHistory,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticResolveHalf,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticResolveGeometry,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticIrradiance,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    ASSERT_EQ(compiledLighting->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const lightingSeed = compiledPlan.findTask(lighting).prologueStateSeeds;
    ASSERT_NE(lightingSeed, nullptr);
    EXPECT_EQ(lightingSeed[0u].resource, causticIrradiance);
    EXPECT_EQ(lightingSeed[0u].sourcePacket, causticsPacket);
    ASSERT_EQ(compiledLighting->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const lightingBarrier = compiledPlan.findTask(lighting).prologueBarriers;
    ASSERT_NE(lightingBarrier, nullptr);
    EXPECT_EQ(lightingBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(lightingBarrier[0u].resource, causticIrradiance);
    EXPECT_EQ(lightingBarrier[0u].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(lightingBarrier[0u].after, Graphics::ResourceStates::ShaderResource);
    ASSERT_EQ(compiledPlan.packet(lightingPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(lightingPacket).dependencies[0u].producer, causticsPacket);
}

TEST(GpuTaskGraph, PlansGraphOwnedCausticPhotonGeometryPrepareFiveWaveletAndUpsampleHandoffs){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_accumulator"),
        "Caustic Photon Resolve Accumulator",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(accumulator.valid());
    const Graphics::GpuGraphResourceId geometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_geometry"),
        "Caustic Photon Resolve Geometry",
        Graphics::ResourceStates::Unknown,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(geometry.valid());
    const Graphics::GpuGraphResourceId history = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_history"),
        "Caustic Photon Resolve History",
        Graphics::ResourceStates::Unknown,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(history.valid());
    const Graphics::GpuGraphResourceId resolveHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_half"),
        "Caustic Photon Resolve Half",
        Graphics::ResourceStates::Unknown,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(resolveHalf.valid());

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    Graphics::GpuTaskSchedulingHint photonScheduling;
    photonScheduling.cost = Graphics::GpuTaskCostHint::Large;
    photonScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse photonUses[] = {
        {
            .resource = accumulator,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc photonDesc;
    photonDesc
        .setIdentity(Name("tests/task_graph/caustic_photon_stage"))
        .setMarkerLabel("Caustic Photons")
        .setQueue(computeRequest)
        .setScheduling(photonScheduling)
        .setResourceUses(photonUses, LengthOf(photonUses))
    ;
    const Graphics::GpuTaskId photonTask = graph.addTask(photonDesc);
    ASSERT_TRUE(photonTask.valid());

    const Graphics::GpuTaskResourceUse geometryUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint geometryScheduling = photonScheduling;
    geometryScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc geometryDesc;
    geometryDesc
        .setIdentity(Name("tests/task_graph/caustic_geometry_stage"))
        .setMarkerLabel("Caustic Geometry")
        .setQueue(computeRequest)
        .setScheduling(geometryScheduling)
        .setDependencies(&photonTask, 1u)
        .setResourceUses(geometryUses, LengthOf(geometryUses))
    ;
    const Graphics::GpuTaskId geometryTask = graph.addTask(geometryDesc);
    ASSERT_TRUE(geometryTask.valid());

    Graphics::GpuTaskSchedulingHint prepareScheduling = geometryScheduling;
    prepareScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse prepareUses[] = {
        {
            .resource = accumulator,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_prepare_stage"))
        .setMarkerLabel("Caustics Resolve Prepare")
        .setQueue(computeRequest)
        .setScheduling(prepareScheduling)
        .setDependencies(&geometryTask, 1u)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    const Graphics::GpuTaskId prepareTask = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepareTask.valid());

    Graphics::GpuTaskSchedulingHint waveletScheduling = prepareScheduling;
    waveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse waveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc waveletDesc;
    waveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Wavelet")
        .setQueue(computeRequest)
        .setScheduling(waveletScheduling)
        .setDependencies(&prepareTask, 1u)
        .setResourceUses(waveletUses, LengthOf(waveletUses))
    ;
    const Graphics::GpuTaskId waveletTask = graph.addTask(waveletDesc);
    ASSERT_TRUE(waveletTask.valid());

    Graphics::GpuTaskSchedulingHint secondWaveletScheduling = waveletScheduling;
    secondWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse secondWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc secondWaveletDesc;
    secondWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_second_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Second Wavelet")
        .setQueue(computeRequest)
        .setScheduling(secondWaveletScheduling)
        .setDependencies(&waveletTask, 1u)
        .setResourceUses(secondWaveletUses, LengthOf(secondWaveletUses))
    ;
    const Graphics::GpuTaskId secondWaveletTask = graph.addTask(secondWaveletDesc);
    ASSERT_TRUE(secondWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint thirdWaveletScheduling = secondWaveletScheduling;
    thirdWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse thirdWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc thirdWaveletDesc;
    thirdWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_third_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Third Wavelet")
        .setQueue(computeRequest)
        .setScheduling(thirdWaveletScheduling)
        .setDependencies(&secondWaveletTask, 1u)
        .setResourceUses(thirdWaveletUses, LengthOf(thirdWaveletUses))
    ;
    const Graphics::GpuTaskId thirdWaveletTask = graph.addTask(thirdWaveletDesc);
    ASSERT_TRUE(thirdWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint fourthWaveletScheduling = thirdWaveletScheduling;
    fourthWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse fourthWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc fourthWaveletDesc;
    fourthWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_fourth_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Fourth Wavelet")
        .setQueue(computeRequest)
        .setScheduling(fourthWaveletScheduling)
        .setDependencies(&thirdWaveletTask, 1u)
        .setResourceUses(fourthWaveletUses, LengthOf(fourthWaveletUses))
    ;
    const Graphics::GpuTaskId fourthWaveletTask = graph.addTask(fourthWaveletDesc);
    ASSERT_TRUE(fourthWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint fifthWaveletScheduling = fourthWaveletScheduling;
    fifthWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse fifthWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc fifthWaveletDesc;
    fifthWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_fifth_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Fifth Wavelet")
        .setQueue(computeRequest)
        .setScheduling(fifthWaveletScheduling)
        .setDependencies(&fourthWaveletTask, 1u)
        .setResourceUses(fifthWaveletUses, LengthOf(fifthWaveletUses))
    ;
    const Graphics::GpuTaskId fifthWaveletTask = graph.addTask(fifthWaveletDesc);
    ASSERT_TRUE(fifthWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint tailScheduling = fifthWaveletScheduling;
    tailScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse tailUses[] = {
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc tailDesc;
    tailDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_upsample_stage"))
        .setMarkerLabel("Caustics Resolve Upsample")
        .setQueue(computeRequest)
        .setScheduling(tailScheduling)
        .setDependencies(&fifthWaveletTask, 1u)
        .setResourceUses(tailUses, LengthOf(tailUses))
    ;
    const Graphics::GpuTaskId tailTask = graph.addTask(tailDesc);
    ASSERT_TRUE(tailTask.valid());

    Graphics::GpuTaskSchedulingHint timingCloseScheduling = tailScheduling;
    timingCloseScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc timingCloseDesc;
    timingCloseDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_timing_close"))
        .setMarkerLabel("Caustics Resolve Timing Close")
        .setQueue(computeRequest)
        .setScheduling(timingCloseScheduling)
        .setDependencies(&tailTask, 1u)
    ;
    const Graphics::GpuTaskId timingCloseTask = graph.addTask(timingCloseDesc);
    ASSERT_TRUE(timingCloseTask.valid());

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

    ASSERT_TRUE(HasInferredHazard(
        analysis,
        photonTask,
        prepareTask,
        accumulator,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        geometryTask,
        prepareTask,
        geometry,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prepareTask,
        waveletTask,
        history,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        waveletTask,
        secondWaveletTask,
        resolveHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        secondWaveletTask,
        thirdWaveletTask,
        history,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        thirdWaveletTask,
        fourthWaveletTask,
        resolveHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        fourthWaveletTask,
        fifthWaveletTask,
        history,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        fifthWaveletTask,
        tailTask,
        resolveHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuSubmissionPacketId photonPacket = compiledPlan.packetForTask(photonTask);
    const Graphics::GpuSubmissionPacketId geometryPacket = compiledPlan.packetForTask(geometryTask);
    const Graphics::GpuSubmissionPacketId preparePacket = compiledPlan.packetForTask(prepareTask);
    const Graphics::GpuSubmissionPacketId waveletPacket = compiledPlan.packetForTask(waveletTask);
    const Graphics::GpuSubmissionPacketId secondWaveletPacket = compiledPlan.packetForTask(secondWaveletTask);
    const Graphics::GpuSubmissionPacketId thirdWaveletPacket = compiledPlan.packetForTask(thirdWaveletTask);
    const Graphics::GpuSubmissionPacketId fourthWaveletPacket = compiledPlan.packetForTask(fourthWaveletTask);
    const Graphics::GpuSubmissionPacketId fifthWaveletPacket = compiledPlan.packetForTask(fifthWaveletTask);
    const Graphics::GpuSubmissionPacketId tailPacket = compiledPlan.packetForTask(tailTask);
    const Graphics::GpuSubmissionPacketId timingClosePacket = compiledPlan.packetForTask(timingCloseTask);
    ASSERT_TRUE(photonPacket.valid());
    ASSERT_TRUE(geometryPacket.valid());
    ASSERT_TRUE(preparePacket.valid());
    ASSERT_TRUE(waveletPacket.valid());
    ASSERT_TRUE(secondWaveletPacket.valid());
    ASSERT_TRUE(thirdWaveletPacket.valid());
    ASSERT_TRUE(fourthWaveletPacket.valid());
    ASSERT_TRUE(fifthWaveletPacket.valid());
    ASSERT_TRUE(tailPacket.valid());
    ASSERT_TRUE(timingClosePacket.valid());
    EXPECT_EQ(compiledPlan.packetCount(), 1u);
    EXPECT_EQ(geometryPacket, photonPacket);
    EXPECT_EQ(preparePacket, photonPacket);
    EXPECT_EQ(waveletPacket, photonPacket);
    EXPECT_EQ(secondWaveletPacket, photonPacket);
    EXPECT_EQ(thirdWaveletPacket, photonPacket);
    EXPECT_EQ(fourthWaveletPacket, photonPacket);
    EXPECT_EQ(fifthWaveletPacket, photonPacket);
    EXPECT_EQ(tailPacket, photonPacket);
    EXPECT_EQ(timingClosePacket, photonPacket);

    const Graphics::GpuCompiledTask* const compiledGeometry = compiledPlan.findTask(geometryTask).plan;
    const Graphics::GpuCompiledTask* const compiledPrepare = compiledPlan.findTask(prepareTask).plan;
    const Graphics::GpuCompiledTask* const compiledWavelet = compiledPlan.findTask(waveletTask).plan;
    const Graphics::GpuCompiledTask* const compiledSecondWavelet = compiledPlan.findTask(secondWaveletTask).plan;
    const Graphics::GpuCompiledTask* const compiledThirdWavelet = compiledPlan.findTask(thirdWaveletTask).plan;
    const Graphics::GpuCompiledTask* const compiledFourthWavelet = compiledPlan.findTask(fourthWaveletTask).plan;
    const Graphics::GpuCompiledTask* const compiledFifthWavelet = compiledPlan.findTask(fifthWaveletTask).plan;
    const Graphics::GpuCompiledTask* const compiledTail = compiledPlan.findTask(tailTask).plan;
    ASSERT_NE(compiledGeometry, nullptr);
    ASSERT_NE(compiledPrepare, nullptr);
    ASSERT_NE(compiledWavelet, nullptr);
    ASSERT_NE(compiledSecondWavelet, nullptr);
    ASSERT_NE(compiledThirdWavelet, nullptr);
    ASSERT_NE(compiledFourthWavelet, nullptr);
    ASSERT_NE(compiledFifthWavelet, nullptr);
    ASSERT_NE(compiledTail, nullptr);
    const Graphics::GpuCompiledBarrier* const geometryBarriers = compiledPlan.findTask(geometryTask).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const prepareBarriers = compiledPlan.findTask(prepareTask).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const waveletBarriers = compiledPlan.findTask(waveletTask).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const secondWaveletBarriers = compiledPlan.findTask(secondWaveletTask).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const thirdWaveletBarriers = compiledPlan.findTask(thirdWaveletTask).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const fourthWaveletBarriers = compiledPlan.findTask(fourthWaveletTask).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const fifthWaveletBarriers = compiledPlan.findTask(fifthWaveletTask).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const tailBarriers = compiledPlan.findTask(tailTask).prologueBarriers;
    ASSERT_NE(geometryBarriers, nullptr);
    ASSERT_NE(prepareBarriers, nullptr);
    ASSERT_NE(waveletBarriers, nullptr);
    ASSERT_NE(secondWaveletBarriers, nullptr);
    ASSERT_NE(thirdWaveletBarriers, nullptr);
    ASSERT_NE(fourthWaveletBarriers, nullptr);
    ASSERT_NE(fifthWaveletBarriers, nullptr);
    ASSERT_NE(tailBarriers, nullptr);
    bool hasFreshGeometryWrite = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledGeometry->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = geometryBarriers[barrierIndex];
        hasFreshGeometryWrite = hasFreshGeometryWrite || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == geometry
            && barrier.before == Graphics::ResourceStates::Unknown
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && !barrier.isGraphInitialState
        );
    }
    EXPECT_TRUE(hasFreshGeometryWrite);

    bool hasAccumulatorHandoff = false;
    bool hasGeometryHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledPrepare->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = prepareBarriers[barrierIndex];
        hasAccumulatorHandoff = hasAccumulatorHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == accumulator
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
        hasGeometryHandoff = hasGeometryHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == geometry
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasAccumulatorHandoff);
    EXPECT_TRUE(hasGeometryHandoff);

    bool hasFreshHistoryWrite = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledPrepare->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = prepareBarriers[barrierIndex];
        hasFreshHistoryWrite = hasFreshHistoryWrite || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == history
            && barrier.before == Graphics::ResourceStates::Unknown
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && !barrier.isGraphInitialState
        );
    }
    EXPECT_TRUE(hasFreshHistoryWrite);

    bool hasPrepareWaveletHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = waveletBarriers[barrierIndex];
        hasPrepareWaveletHandoff = hasPrepareWaveletHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == history
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasPrepareWaveletHandoff);

    bool hasFreshResolveHalfWrite = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = waveletBarriers[barrierIndex];
        hasFreshResolveHalfWrite = hasFreshResolveHalfWrite || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalf
            && barrier.before == Graphics::ResourceStates::Unknown
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && !barrier.isGraphInitialState
        );
    }
    EXPECT_TRUE(hasFreshResolveHalfWrite);

    bool hasWaveletSecondHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledSecondWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = secondWaveletBarriers[barrierIndex];
        hasWaveletSecondHandoff = hasWaveletSecondHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasWaveletSecondHandoff);

    bool hasSecondThirdHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledThirdWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = thirdWaveletBarriers[barrierIndex];
        hasSecondThirdHandoff = hasSecondThirdHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == history
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasSecondThirdHandoff);

    bool hasThirdFourthHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledFourthWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = fourthWaveletBarriers[barrierIndex];
        hasThirdFourthHandoff = hasThirdFourthHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasThirdFourthHandoff);

    bool hasFourthFifthHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledFifthWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = fifthWaveletBarriers[barrierIndex];
        hasFourthFifthHandoff = hasFourthFifthHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == history
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasFourthFifthHandoff);

    bool hasFifthUpsampleHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledTail->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = tailBarriers[barrierIndex];
        hasFifthUpsampleHandoff = hasFifthUpsampleHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasFifthUpsampleHandoff);
}

TEST(GpuTaskGraph, PlansGraphOwnedHardwareCausticsEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_world_position"),
        "Hardware Caustics World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_depth"),
        "Hardware Caustics Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId irradiance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_irradiance"),
        "Hardware Caustics Irradiance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_accumulator"),
        "Hardware Caustics Accumulator",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId meshAttributes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_mesh_attributes"),
        "Hardware Caustics Mesh Attributes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId instanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_instance_materials"),
        "Hardware Caustics Instance Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId typedMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_typed_materials"),
        "Hardware Caustics Typed Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId instances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_instances"),
        "Hardware Caustics Instances",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId emissionTargets = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_emission_targets"),
        "Hardware Caustics Emission Targets",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_lights"),
        "Hardware Caustics Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId meshView = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_mesh_view"),
        "Hardware Caustics Mesh View",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId bindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_bindless_slots"),
        "Hardware Caustics Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_material_context_slots"),
        "Hardware Caustics Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_scene_shading"),
        "Hardware Caustics Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(irradiance.valid());
    ASSERT_TRUE(accumulator.valid());
    ASSERT_TRUE(meshAttributes.valid());
    ASSERT_TRUE(instanceMaterials.valid());
    ASSERT_TRUE(typedMaterials.valid());
    ASSERT_TRUE(instances.valid());
    ASSERT_TRUE(emissionTargets.valid());
    ASSERT_TRUE(lights.valid());
    ASSERT_TRUE(meshView.valid());
    ASSERT_TRUE(bindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(sceneShading.valid());

    const Graphics::GpuGraphResourceSetId meshAttributesSet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/hardware_caustics_mesh_attributes"))
            .setMarkerLabel("Hardware Caustics Mesh Attributes")
            .setMembers(&meshAttributes, 1u)
    );
    ASSERT_TRUE(meshAttributesSet.valid());
    const Graphics::GpuTaskResourceSetUse meshAttributesSetUses[] = {
        {
            .resourceSet = meshAttributesSet,
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
    const Graphics::GpuQueueRequest graphicsTransferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::DepthWrite, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = meshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = instanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = typedMaterials, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = instances, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = emissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = bindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/hardware_caustics_prefix"))
        .setMarkerLabel("Hardware Caustics Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    // Keep the no-producer black result in the existing Hardware Caustics Graphics packet.
    Graphics::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse irradianceClearUses[] = {
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_hardware_caustics_irradiance_clear"))
        .setMarkerLabel("Hardware Caustics Irradiance Clear")
        .setQueue(graphicsTransferRequest)
        .setScheduling(irradianceClearScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(irradianceClearUses, LengthOf(irradianceClearUses))
    ;
    const Graphics::GpuTaskId irradianceClearTask = graph.addTask(irradianceClearDesc);
    ASSERT_TRUE(irradianceClearTask.valid());

    const Graphics::GpuTaskResourceUse accumulatorBootstrapClearUses[] = {
        { .resource = accumulator, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling = irradianceClearScheduling;
    accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc accumulatorBootstrapClearDesc;
    accumulatorBootstrapClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_hardware_caustics_accumulator_bootstrap_clear"))
        .setMarkerLabel("Hardware Caustics Accumulator Bootstrap Clear")
        .setQueue(graphicsTransferRequest)
        .setScheduling(accumulatorBootstrapClearScheduling)
        .setDependencies(&irradianceClearTask, 1u)
        .setResourceUses(accumulatorBootstrapClearUses, LengthOf(accumulatorBootstrapClearUses))
    ;
    const Graphics::GpuTaskId accumulatorBootstrapClearTask = graph.addTask(accumulatorBootstrapClearDesc);
    ASSERT_TRUE(accumulatorBootstrapClearTask.valid());

    const Graphics::GpuTaskResourceUse causticsUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = instanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = typedMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = instances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = emissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = bindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = accumulator, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint causticsScheduling = boundaryScheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc causticsDesc;
    causticsDesc
        .setIdentity(Name("tests/task_graph/graph_owned_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(graphicsRequest)
        .setScheduling(causticsScheduling)
        .setDependencies(&accumulatorBootstrapClearTask, 1u)
        .setResourceUses(causticsUses, LengthOf(causticsUses))
        .setResourceSetUses(meshAttributesSetUses, LengthOf(meshAttributesSetUses))
    ;
    const Graphics::GpuTaskId caustics = graph.addTask(causticsDesc);
    ASSERT_TRUE(caustics.valid());

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

    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prefix,
        caustics,
        meshAttributes,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        irradianceClearTask,
        caustics,
        irradiance,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        accumulatorBootstrapClearTask,
        caustics,
        accumulator,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const causticsAssignment = assignments.find(caustics);
    ASSERT_NE(causticsAssignment, nullptr);
    EXPECT_EQ(causticsAssignment->queueClass, Graphics::CommandQueue::Graphics);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId irradianceClearPacket = compiledPlan.packetForTask(irradianceClearTask);
    const Graphics::GpuSubmissionPacketId accumulatorBootstrapClearPacket =
        compiledPlan.packetForTask(accumulatorBootstrapClearTask);
    const Graphics::GpuSubmissionPacketId causticsPacket = compiledPlan.packetForTask(caustics);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(irradianceClearPacket.valid());
    ASSERT_TRUE(accumulatorBootstrapClearPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    EXPECT_NE(prefixPacket, causticsPacket);
    EXPECT_EQ(irradianceClearPacket, causticsPacket);
    EXPECT_EQ(accumulatorBootstrapClearPacket, causticsPacket);
    EXPECT_EQ(compiledPlan.packetCount(), 2u);
    const Graphics::GpuCompiledTask* const compiledCaustics = compiledPlan.findTask(caustics).plan;
    ASSERT_NE(compiledCaustics, nullptr);
    const Graphics::GpuCompiledBarrier* const causticsBarriers = compiledPlan.findTask(caustics).prologueBarriers;
    ASSERT_NE(causticsBarriers, nullptr);
    const auto hasCausticsBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledCaustics->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = causticsBarriers[barrierIndex];
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
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        depth,
        Graphics::ResourceStates::DepthWrite,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        worldPosition,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        meshAttributes,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        meshView,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneShading,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradiance,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        accumulator,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    ASSERT_EQ(compiledPlan.packet(causticsPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(causticsPacket).dependencies[0u].producer, prefixPacket);
}

TEST(GpuTaskGraph, PlansNonTemporalCausticAccumulatorClearBeforePhotonProducer){
    const auto verifyRoute = [](const bool hardwareCaustics){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
            graph,
            Name("tests/task_graph/non_temporal_caustics_accumulator"),
            "Non-Temporal Caustic Accumulator",
            Graphics::ResourceStates::ShaderResource,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
        );
        ASSERT_TRUE(accumulator.valid());

        const Graphics::GpuQueueRequest graphicsUploadRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        };
        const Graphics::GpuQueueRequest computeTransferRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Compute,
            true,
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
            true,
            false,
        };
        Graphics::GpuTaskSchedulingHint clearScheduling;
        clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
        clearScheduling.allowPacketMerge = true;
        const Graphics::GpuTaskResourceUse clearUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::CopyDest,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        Graphics::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(Name("tests/task_graph/non_temporal_caustics_accumulator_clear"))
            .setMarkerLabel("Caustic Accumulator Clear")
            .setQueue(hardwareCaustics ? graphicsUploadRequest : computeTransferRequest)
            .setScheduling(clearScheduling)
            .setResourceUses(clearUses, LengthOf(clearUses))
        ;
        const Graphics::GpuTaskId clear = graph.addTask(clearDesc);
        ASSERT_TRUE(clear.valid());

        Graphics::GpuTaskSchedulingHint producerScheduling;
        producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
        producerScheduling.allowPacketMerge = true;
        producerScheduling.mergeWithPrevious = true;
        const Graphics::GpuTaskResourceUse producerUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = Graphics::GpuTaskResourceAccess::ReadWrite,
            },
        };
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(Name("tests/task_graph/non_temporal_caustics_photon_producer"))
            .setMarkerLabel(hardwareCaustics ? "Hardware Caustics" : "Software Caustics")
            .setQueue(hardwareCaustics ? graphicsRequest : computeRequest)
            .setScheduling(producerScheduling)
            .setDependencies(&clear, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
        ASSERT_TRUE(producer.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphCompileOptions compileOptions;
        compileOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_TRUE(HasInferredHazard(
            analysis,
            clear,
            producer,
            accumulator,
            Graphics::GpuTaskHazardType::WriteAfterWrite
        ));

        const Graphics::GpuTaskQueueAssignment* const clearAssignment = assignments.find(clear);
        const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
        ASSERT_NE(clearAssignment, nullptr);
        ASSERT_NE(producerAssignment, nullptr);
        const Graphics::CommandQueue::Enum expectedQueue = hardwareCaustics
            ? Graphics::CommandQueue::Graphics
            : Graphics::CommandQueue::Compute
        ;
        EXPECT_EQ(clearAssignment->queueClass, expectedQueue);
        EXPECT_EQ(producerAssignment->queueClass, expectedQueue);

        const Graphics::GpuSubmissionPacketId clearPacket = compiledPlan.packetForTask(clear);
        const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
        ASSERT_TRUE(clearPacket.valid());
        EXPECT_EQ(producerPacket, clearPacket);
        ASSERT_EQ(compiledPlan.packetCount(), 1u);
        const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(clearPacket).plan;
        ASSERT_EQ(packet.taskCount, 2u);
        ASSERT_NE(compiledPlan.packet(clearPacket).tasks, nullptr);
        EXPECT_EQ(compiledPlan.packet(clearPacket).tasks[0u], clear);
        EXPECT_EQ(compiledPlan.packet(clearPacket).tasks[1u], producer);

        const Graphics::GpuCompiledTask* const compiledClear = compiledPlan.findTask(clear).plan;
        const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
        ASSERT_NE(compiledClear, nullptr);
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_EQ(compiledClear->prologueBarrierCount, 1u);
        ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const clearBarrier = compiledPlan.findTask(clear).prologueBarriers;
        const Graphics::GpuCompiledBarrier* const producerBarrier = compiledPlan.findTask(producer).prologueBarriers;
        ASSERT_NE(clearBarrier, nullptr);
        ASSERT_NE(producerBarrier, nullptr);
        EXPECT_EQ(clearBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(clearBarrier[0u].resource, accumulator);
        EXPECT_EQ(clearBarrier[0u].before, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(clearBarrier[0u].after, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(producerBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(producerBarrier[0u].resource, accumulator);
        EXPECT_EQ(producerBarrier[0u].before, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(producerBarrier[0u].after, Graphics::ResourceStates::UnorderedAccess);
    };

    verifyRoute(false);
    verifyRoute(true);
}

TEST(GpuTaskGraph, PlansWarmCausticAccumulatorDecayBeforePhotonProducer){
    const auto verifyRoute = [](const bool hardwareCaustics){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
            graph,
            Name("tests/task_graph/warm_caustics_accumulator"),
            "Warm Caustic Accumulator",
            Graphics::ResourceStates::ShaderResource,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
        );
        ASSERT_TRUE(accumulator.valid());

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
        Graphics::GpuTaskSchedulingHint decayScheduling;
        decayScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
        decayScheduling.allowPacketMerge = true;
        const Graphics::GpuTaskResourceUse decayUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = Graphics::GpuTaskResourceAccess::ReadWrite,
            },
        };
        Graphics::GpuTaskDesc decayDesc;
        decayDesc
            .setIdentity(Name("tests/task_graph/warm_caustics_accumulator_decay"))
            .setMarkerLabel("Caustic Accumulator Decay")
            .setQueue(hardwareCaustics ? graphicsRequest : computeRequest)
            .setScheduling(decayScheduling)
            .setResourceUses(decayUses, LengthOf(decayUses))
        ;
        const Graphics::GpuTaskId decay = graph.addTask(decayDesc);
        ASSERT_TRUE(decay.valid());

        Graphics::GpuTaskSchedulingHint producerScheduling = decayScheduling;
        producerScheduling.mergeWithPrevious = true;
        const Graphics::GpuTaskResourceUse producerUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = Graphics::GpuTaskResourceAccess::ReadWrite,
            },
        };
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(Name("tests/task_graph/warm_caustics_photon_producer"))
            .setMarkerLabel(hardwareCaustics ? "Hardware Caustics" : "Software Caustics")
            .setQueue(hardwareCaustics ? graphicsRequest : computeRequest)
            .setScheduling(producerScheduling)
            .setDependencies(&decay, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
        ASSERT_TRUE(producer.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphCompileOptions compileOptions;
        compileOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_TRUE(HasInferredHazard(
            analysis,
            decay,
            producer,
            accumulator,
            Graphics::GpuTaskHazardType::WriteAfterWrite
        ));

        const Graphics::GpuTaskQueueAssignment* const decayAssignment = assignments.find(decay);
        const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
        ASSERT_NE(decayAssignment, nullptr);
        ASSERT_NE(producerAssignment, nullptr);
        const Graphics::CommandQueue::Enum expectedQueue = hardwareCaustics
            ? Graphics::CommandQueue::Graphics
            : Graphics::CommandQueue::Compute
        ;
        EXPECT_EQ(decayAssignment->queueClass, expectedQueue);
        EXPECT_EQ(producerAssignment->queueClass, expectedQueue);

        const Graphics::GpuSubmissionPacketId decayPacket = compiledPlan.packetForTask(decay);
        const Graphics::GpuSubmissionPacketId producerPacket = compiledPlan.packetForTask(producer);
        ASSERT_TRUE(decayPacket.valid());
        EXPECT_EQ(producerPacket, decayPacket);
        ASSERT_EQ(compiledPlan.packetCount(), 1u);
        const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(decayPacket).plan;
        ASSERT_EQ(packet.taskCount, 2u);
        ASSERT_NE(compiledPlan.packet(decayPacket).tasks, nullptr);
        EXPECT_EQ(compiledPlan.packet(decayPacket).tasks[0u], decay);
        EXPECT_EQ(compiledPlan.packet(decayPacket).tasks[1u], producer);

        const Graphics::GpuCompiledTask* const compiledDecay = compiledPlan.findTask(decay).plan;
        const Graphics::GpuCompiledTask* const compiledProducer = compiledPlan.findTask(producer).plan;
        ASSERT_NE(compiledDecay, nullptr);
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_EQ(compiledDecay->prologueBarrierCount, 1u);
        ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const decayBarrier = compiledPlan.findTask(decay).prologueBarriers;
        const Graphics::GpuCompiledBarrier* const producerBarrier = compiledPlan.findTask(producer).prologueBarriers;
        ASSERT_NE(decayBarrier, nullptr);
        ASSERT_NE(producerBarrier, nullptr);
        EXPECT_EQ(decayBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(decayBarrier[0u].resource, accumulator);
        EXPECT_EQ(decayBarrier[0u].before, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(decayBarrier[0u].after, Graphics::ResourceStates::UnorderedAccess);
        EXPECT_EQ(producerBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureUav);
        EXPECT_EQ(producerBarrier[0u].resource, accumulator);
        EXPECT_EQ(producerBarrier[0u].before, Graphics::ResourceStates::UnorderedAccess);
        EXPECT_EQ(producerBarrier[0u].after, Graphics::ResourceStates::UnorderedAccess);
    };

    verifyRoute(false);
    verifyRoute(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

