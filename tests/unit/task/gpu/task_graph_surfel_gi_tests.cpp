// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_surfel_gi_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansGraphOwnedSurfelGiResolveAndEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_world_position"),
        "Surfel GI World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId normal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_normal"),
        "Surfel GI Normal",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId irradianceHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_irradiance_half"),
        "Surfel GI Irradiance Half",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId irradiance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_irradiance"),
        "Surfel GI Irradiance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_bindless_slots"),
        "Surfel GI Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_material_context_slots"),
        "Surfel GI Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId surfelConstants = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_constants"),
        "Surfel GI Constants",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_scene_shading"),
        "Surfel GI Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId traceGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_trace_geometry"),
        "Surfel GI Trace Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId poolSnapshot = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_pool_snapshot"),
        "Surfel GI Pool Snapshot",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId cellHeadSnapshot = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_cell_head_snapshot"),
        "Surfel GI Cell Head Snapshot",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_lights"),
        "Surfel GI Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId pool = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_pool"),
        "Surfel GI Pool",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId cellHeads = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_cell_heads"),
        "Surfel GI Cell Heads",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId counter = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_counter"),
        "Surfel GI Counter",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId traceArgs = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_trace_args"),
        "Surfel GI Trace Arguments",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId freeList = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_free_list"),
        "Surfel GI Free List",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(normal.valid());
    ASSERT_TRUE(irradianceHalf.valid());
    ASSERT_TRUE(irradiance.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(surfelConstants.valid());
    ASSERT_TRUE(sceneShading.valid());
    ASSERT_TRUE(traceGeometry.valid());
    ASSERT_TRUE(poolSnapshot.valid());
    ASSERT_TRUE(cellHeadSnapshot.valid());
    ASSERT_TRUE(lights.valid());
    ASSERT_TRUE(pool.valid());
    ASSERT_TRUE(cellHeads.valid());
    ASSERT_TRUE(counter.valid());
    ASSERT_TRUE(traceArgs.valid());
    ASSERT_TRUE(freeList.valid());

    const Graphics::GpuGraphResourceSetId traceGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/surfel_gi_trace_geometry"))
            .setMarkerLabel("Surfel GI Trace Geometry")
            .setMembers(&traceGeometry, 1u)
    );
    ASSERT_TRUE(traceGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse traceGeometrySetUses[] = {
        {
            .resourceSet = traceGeometrySet,
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

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = irradianceHalf, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = traceGeometry, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = poolSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeadSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = traceArgs, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/surfel_gi_prefix"))
        .setMarkerLabel("Surfel GI Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    // The typed production clear starts a new Compute packet after the prefix; Surfel GI opts into merging with it,
    // so the CopyDest-to-UAV handoff stays graph-owned without adding a packet to the semantic GI range.
    Graphics::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    clearScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse clearUses[] = {
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_output_clear"))
        .setMarkerLabel("Surfel GI Output Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(clearScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(clearUses, LengthOf(clearUses))
    ;
    const Graphics::GpuTaskId outputClear = graph.addTask(clearDesc);
    ASSERT_TRUE(outputClear.valid());

    Graphics::GpuTaskSchedulingHint surfelScheduling = boundaryScheduling;
    surfelScheduling.forceSubmissionBoundary = false;
    surfelScheduling.allowPacketMerge = true;
    surfelScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse ageFreeUses[] = {
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc ageFreeDesc;
    ageFreeDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_age_free"))
        .setMarkerLabel("Surfel GI Age Free")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&outputClear, 1u)
        .setResourceUses(ageFreeUses, LengthOf(ageFreeUses))
    ;
    const Graphics::GpuTaskId ageFree = graph.addTask(ageFreeDesc);
    ASSERT_TRUE(ageFree.valid());

    const Graphics::GpuTaskResourceUse cellHeadClearUses[] = {
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc cellHeadClearDesc;
    cellHeadClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_cell_head_clear"))
        .setMarkerLabel("Surfel GI Cell Head Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&ageFree, 1u)
        .setResourceUses(cellHeadClearUses, LengthOf(cellHeadClearUses))
    ;
    const Graphics::GpuTaskId cellHeadClear = graph.addTask(cellHeadClearDesc);
    ASSERT_TRUE(cellHeadClear.valid());

    const Graphics::GpuTaskResourceUse hashBuildUses[] = {
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc hashBuildDesc;
    hashBuildDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_hash_build"))
        .setMarkerLabel("Surfel GI Hash Build")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&cellHeadClear, 1u)
        .setResourceUses(hashBuildUses, LengthOf(hashBuildUses))
    ;
    const Graphics::GpuTaskId hashBuild = graph.addTask(hashBuildDesc);
    ASSERT_TRUE(hashBuild.valid());

    const Graphics::GpuTaskResourceUse spawnUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc spawnDesc;
    spawnDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_spawn"))
        .setMarkerLabel("Surfel GI Spawn")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&hashBuild, 1u)
        .setResourceUses(spawnUses, LengthOf(spawnUses))
    ;
    const Graphics::GpuTaskId spawn = graph.addTask(spawnDesc);
    ASSERT_TRUE(spawn.valid());

    const Graphics::GpuTaskResourceUse traceBuildArgsUses[] = {
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = traceArgs, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc traceBuildArgsDesc;
    traceBuildArgsDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_trace_build_args"))
        .setMarkerLabel("Surfel GI Trace Build Args")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&spawn, 1u)
        .setResourceUses(traceBuildArgsUses, LengthOf(traceBuildArgsUses))
    ;
    const Graphics::GpuTaskId traceBuildArgs = graph.addTask(traceBuildArgsDesc);
    ASSERT_TRUE(traceBuildArgs.valid());

    const Graphics::GpuTaskResourceUse traceUses[] = {
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = poolSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = cellHeadSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = traceArgs, .range = {}, .requiredState = Graphics::ResourceStates::IndirectArgument, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    Graphics::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_trace"))
        .setMarkerLabel("Surfel GI Trace")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&traceBuildArgs, 1u)
        .setResourceUses(traceUses, LengthOf(traceUses))
        .setResourceSetUses(traceGeometrySetUses, LengthOf(traceGeometrySetUses))
    ;
    const Graphics::GpuTaskId trace = graph.addTask(traceDesc);
    ASSERT_TRUE(trace.valid());

    const Graphics::GpuTaskResourceUse resolveUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = irradianceHalf, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc resolveDesc;
    resolveDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_resolve"))
        .setMarkerLabel("Surfel GI Resolve")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&trace, 1u)
        .setResourceUses(resolveUses, LengthOf(resolveUses))
    ;
    const Graphics::GpuTaskId resolve = graph.addTask(resolveDesc);
    ASSERT_TRUE(resolve.valid());

    const Graphics::GpuTaskResourceUse surfelUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = irradianceHalf, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc surfelDesc;
    surfelDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&resolve, 1u)
        .setResourceUses(surfelUses, LengthOf(surfelUses))
    ;
    const Graphics::GpuTaskId surfelGi = graph.addTask(surfelDesc);
    ASSERT_TRUE(surfelGi.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&surfelGi, 1u)
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
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    EXPECT_TRUE(analysis.hasExplicitEdge(ageFree, cellHeadClear));
    EXPECT_TRUE(analysis.hasExplicitEdge(cellHeadClear, hashBuild));
    EXPECT_TRUE(analysis.hasExplicitEdge(hashBuild, spawn));
    EXPECT_TRUE(analysis.hasExplicitEdge(spawn, traceBuildArgs));
    EXPECT_TRUE(analysis.hasExplicitEdge(traceBuildArgs, trace));
    EXPECT_TRUE(analysis.hasExplicitEdge(trace, resolve));
    EXPECT_TRUE(analysis.hasExplicitEdge(resolve, surfelGi));
    EXPECT_TRUE(analysis.hasExplicitEdge(surfelGi, lighting));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prefix,
        ageFree,
        pool,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    ASSERT_TRUE(HasInferredHazard(
        analysis,
        outputClear,
        surfelGi,
        irradiance,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        ageFree,
        hashBuild,
        pool,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        cellHeadClear,
        hashBuild,
        cellHeads,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        hashBuild,
        spawn,
        cellHeads,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        spawn,
        traceBuildArgs,
        counter,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        traceBuildArgs,
        trace,
        traceArgs,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        trace,
        resolve,
        pool,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        spawn,
        resolve,
        cellHeads,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        resolve,
        surfelGi,
        irradianceHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        surfelGi,
        lighting,
        irradiance,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const clearAssignment = assignments.find(outputClear);
    ASSERT_NE(clearAssignment, nullptr);
    EXPECT_EQ(clearAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuTaskQueueAssignment* const surfelAssignment = assignments.find(surfelGi);
    ASSERT_NE(surfelAssignment, nullptr);
    EXPECT_EQ(surfelAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    ASSERT_NE(lightingAssignment, nullptr);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId clearPacket = compiledPlan.packetForTask(outputClear);
    const Graphics::GpuSubmissionPacketId ageFreePacket = compiledPlan.packetForTask(ageFree);
    const Graphics::GpuSubmissionPacketId cellHeadClearPacket = compiledPlan.packetForTask(cellHeadClear);
    const Graphics::GpuSubmissionPacketId hashBuildPacket = compiledPlan.packetForTask(hashBuild);
    const Graphics::GpuSubmissionPacketId spawnPacket = compiledPlan.packetForTask(spawn);
    const Graphics::GpuSubmissionPacketId traceBuildArgsPacket = compiledPlan.packetForTask(traceBuildArgs);
    const Graphics::GpuSubmissionPacketId tracePacket = compiledPlan.packetForTask(trace);
    const Graphics::GpuSubmissionPacketId resolvePacket = compiledPlan.packetForTask(resolve);
    const Graphics::GpuSubmissionPacketId surfelPacket = compiledPlan.packetForTask(surfelGi);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(ageFreePacket.valid());
    ASSERT_TRUE(cellHeadClearPacket.valid());
    ASSERT_TRUE(hashBuildPacket.valid());
    ASSERT_TRUE(spawnPacket.valid());
    ASSERT_TRUE(traceBuildArgsPacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    ASSERT_TRUE(resolvePacket.valid());
    ASSERT_TRUE(surfelPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_NE(prefixPacket, surfelPacket);
    EXPECT_EQ(clearPacket, surfelPacket);
    EXPECT_EQ(ageFreePacket, surfelPacket);
    EXPECT_EQ(cellHeadClearPacket, surfelPacket);
    EXPECT_EQ(hashBuildPacket, surfelPacket);
    EXPECT_EQ(spawnPacket, surfelPacket);
    EXPECT_EQ(traceBuildArgsPacket, surfelPacket);
    EXPECT_EQ(tracePacket, surfelPacket);
    EXPECT_EQ(resolvePacket, surfelPacket);
    EXPECT_NE(lightingPacket, surfelPacket);
    EXPECT_EQ(compiledPlan.packetCount(), 3u);
    ASSERT_NE(compiledPlan.packet(surfelPacket).tasks, nullptr);
    ASSERT_EQ(compiledPlan.packet(surfelPacket).plan->taskCount, 9u);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[0u], outputClear);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[1u], ageFree);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[2u], cellHeadClear);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[3u], hashBuild);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[4u], spawn);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[5u], traceBuildArgs);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[6u], trace);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[7u], resolve);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).tasks[8u], surfelGi);
    const Graphics::GpuCompiledTask* const compiledAgeFree = compiledPlan.findTask(ageFree).plan;
    ASSERT_NE(compiledAgeFree, nullptr);
    const Graphics::GpuPacketStateSeed* const ageFreeSeeds = compiledPlan.findTask(ageFree).prologueStateSeeds;
    ASSERT_NE(ageFreeSeeds, nullptr);
    const auto hasAgeFreeSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledAgeFree->prologueStateSeedCount; ++seedIndex){
            if(
                ageFreeSeeds[seedIndex].resource == resource
                && ageFreeSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasAgeFreeSeed(surfelConstants));
    EXPECT_TRUE(hasAgeFreeSeed(pool));
    EXPECT_TRUE(hasAgeFreeSeed(counter));
    EXPECT_TRUE(hasAgeFreeSeed(freeList));
    const Graphics::GpuCompiledTask* const compiledSpawn = compiledPlan.findTask(spawn).plan;
    ASSERT_NE(compiledSpawn, nullptr);
    const Graphics::GpuPacketStateSeed* const spawnSeeds = compiledPlan.findTask(spawn).prologueStateSeeds;
    ASSERT_NE(spawnSeeds, nullptr);
    const auto hasSpawnSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledSpawn->prologueStateSeedCount; ++seedIndex){
            if(
                spawnSeeds[seedIndex].resource == resource
                && spawnSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasSpawnSeed(worldPosition));
    EXPECT_TRUE(hasSpawnSeed(normal));
    const Graphics::GpuCompiledTask* const compiledTraceBuildArgs = compiledPlan.findTask(traceBuildArgs).plan;
    ASSERT_NE(compiledTraceBuildArgs, nullptr);
    const Graphics::GpuPacketStateSeed* const traceBuildArgsSeeds = compiledPlan.findTask(traceBuildArgs).prologueStateSeeds;
    ASSERT_NE(traceBuildArgsSeeds, nullptr);
    const auto hasTraceBuildArgsSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledTraceBuildArgs->prologueStateSeedCount; ++seedIndex){
            if(
                traceBuildArgsSeeds[seedIndex].resource == resource
                && traceBuildArgsSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTraceBuildArgsSeed(traceArgs));
    const Graphics::GpuCompiledTask* const compiledTrace = compiledPlan.findTask(trace).plan;
    ASSERT_NE(compiledTrace, nullptr);
    const Graphics::GpuPacketStateSeed* const traceSeeds = compiledPlan.findTask(trace).prologueStateSeeds;
    ASSERT_NE(traceSeeds, nullptr);
    const auto hasTraceSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledTrace->prologueStateSeedCount; ++seedIndex){
            if(
                traceSeeds[seedIndex].resource == resource
                && traceSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTraceSeed(currentBindlessSlots));
    EXPECT_TRUE(hasTraceSeed(traceGeometry));
    EXPECT_TRUE(hasTraceSeed(poolSnapshot));
    const Graphics::GpuCompiledTask* const compiledResolve = compiledPlan.findTask(resolve).plan;
    ASSERT_NE(compiledResolve, nullptr);
    const Graphics::GpuPacketStateSeed* const resolveSeeds = compiledPlan.findTask(resolve).prologueStateSeeds;
    ASSERT_NE(resolveSeeds, nullptr);
    const auto hasResolveSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledResolve->prologueStateSeedCount; ++seedIndex){
            if(
                resolveSeeds[seedIndex].resource == resource
                && resolveSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasResolveSeed(irradianceHalf));

    const Graphics::GpuCompiledBarrier* const ageFreeBarriers = compiledPlan.findTask(ageFree).prologueBarriers;
    ASSERT_NE(ageFreeBarriers, nullptr);
    const auto hasAgeFreeBarrier = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledAgeFree->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = ageFreeBarriers[barrierIndex];
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
    EXPECT_TRUE(hasAgeFreeBarrier(
        surfelConstants,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasAgeFreeBarrier(
        pool,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasAgeFreeBarrier(
        counter,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasAgeFreeBarrier(
        freeList,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledTask* const compiledHashBuild = compiledPlan.findTask(hashBuild).plan;
    ASSERT_NE(compiledHashBuild, nullptr);
    const Graphics::GpuCompiledBarrier* const hashBuildBarriers = compiledPlan.findTask(hashBuild).prologueBarriers;
    ASSERT_NE(hashBuildBarriers, nullptr);
    const auto hasHashBuildBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledHashBuild->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = hashBuildBarriers[barrierIndex];
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
    EXPECT_TRUE(hasHashBuildBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        cellHeads,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledBarrier* const spawnBarriers = compiledPlan.findTask(spawn).prologueBarriers;
    ASSERT_NE(spawnBarriers, nullptr);
    const auto hasSpawnBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledSpawn->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = spawnBarriers[barrierIndex];
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
    EXPECT_TRUE(hasSpawnBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        worldPosition,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasSpawnBarrier(
        Graphics::GpuCompiledBarrierType::BufferUav,
        cellHeads,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledBarrier* const traceBuildArgsBarriers = compiledPlan.findTask(traceBuildArgs).prologueBarriers;
    ASSERT_NE(traceBuildArgsBarriers, nullptr);
    const auto hasTraceBuildArgsBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledTraceBuildArgs->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = traceBuildArgsBarriers[barrierIndex];
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
    EXPECT_TRUE(hasTraceBuildArgsBarrier(
        Graphics::GpuCompiledBarrierType::BufferUav,
        counter,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTraceBuildArgsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        traceArgs,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledBarrier* const traceBarriers = compiledPlan.findTask(trace).prologueBarriers;
    ASSERT_NE(traceBarriers, nullptr);
    const auto hasTraceBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledTrace->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
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
    EXPECT_TRUE(hasTraceBarrier(
        Graphics::GpuCompiledBarrierType::BufferUav,
        pool,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTraceBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        traceArgs,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::IndirectArgument
    ));

    const Graphics::GpuCompiledBarrier* const resolveBarriers = compiledPlan.findTask(resolve).prologueBarriers;
    ASSERT_NE(resolveBarriers, nullptr);
    const auto hasResolveBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledResolve->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = resolveBarriers[barrierIndex];
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
    EXPECT_TRUE(hasResolveBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        pool,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasResolveBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        cellHeads,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasResolveBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradianceHalf,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    const Graphics::GpuCompiledTask* const compiledSurfel = compiledPlan.findTask(surfelGi).plan;
    ASSERT_NE(compiledSurfel, nullptr);
    const Graphics::GpuCompiledBarrier* const surfelBarriers = compiledPlan.findTask(surfelGi).prologueBarriers;
    ASSERT_NE(surfelBarriers, nullptr);
    const auto hasSurfelBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledSurfel->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = surfelBarriers[barrierIndex];
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
    EXPECT_TRUE(hasSurfelBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradianceHalf,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasSurfelBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradiance,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    ASSERT_EQ(compiledPlan.packet(surfelPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(surfelPacket).dependencies[0u].producer, prefixPacket);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledPlan.findTask(lighting).plan;
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuCompiledBarrier* const lightingBarriers = compiledPlan.findTask(lighting).prologueBarriers;
    ASSERT_NE(lightingBarriers, nullptr);
    bool lightingTransitionsIrradiance = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledLighting->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = lightingBarriers[barrierIndex];
        lightingTransitionsIrradiance = lightingTransitionsIrradiance
            || (
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == irradiance
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
        ;
    }
    EXPECT_TRUE(lightingTransitionsIrradiance);
    ASSERT_EQ(compiledPlan.packet(lightingPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(lightingPacket).dependencies[0u].producer, surfelPacket);
}

TEST(GpuTaskGraph, PlansGraphOwnedSurfelInitializationEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer
    ;
    const Graphics::GpuGraphResourceId pool = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_pool"),
        "Surfel Pool",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId cellHeads = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_cell_heads"),
        "Surfel Cell Heads",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId counter = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_counter"),
        "Surfel Counter",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId freeList = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_free_list"),
        "Surfel Free List",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId poolSnapshot = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_pool_snapshot"),
        "Surfel Pool Snapshot",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId cellHeadsSnapshot = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_cell_heads_snapshot"),
        "Surfel Cell Heads Snapshot",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(pool.valid());
    ASSERT_TRUE(cellHeads.valid());
    ASSERT_TRUE(counter.valid());
    ASSERT_TRUE(freeList.valid());
    ASSERT_TRUE(poolSnapshot.valid());
    ASSERT_TRUE(cellHeadsSnapshot.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
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
    const Graphics::GpuQueueRequest transferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/surfel_initialize_prefix"))
        .setMarkerLabel("Surfel Initialize Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    Graphics::GpuTaskSchedulingHint firstClearScheduling;
    firstClearScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    firstClearScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint chainedClearScheduling = firstClearScheduling;
    chainedClearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    chainedClearScheduling.mergeWithPrevious = true;
    chainedClearScheduling.allowMergeAcrossConsumerFrontier = true;
    const auto addInitializationClear = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::GpuTaskId dependency,
        const Graphics::GpuTaskSchedulingHint& scheduling
    ){
        const Graphics::GpuTaskResourceUse clearUse{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(computeTransferRequest)
            .setScheduling(scheduling)
            .setDependencies(&dependency, 1u)
            .setResourceUses(&clearUse, 1u)
        ;
        return graph.addTask(clearDesc);
    };
    const Graphics::GpuTaskId poolClear = addInitializationClear(
        Name("tests/task_graph/surfel_initialize_pool_clear"),
        "Surfel GI Initialize Pool Clear",
        pool,
        prefix,
        firstClearScheduling
    );
    ASSERT_TRUE(poolClear.valid());
    const Graphics::GpuTaskId cellHeadClear = addInitializationClear(
        Name("tests/task_graph/surfel_initialize_cell_head_clear"),
        "Surfel GI Initialize Cell-Head Clear",
        cellHeads,
        poolClear,
        chainedClearScheduling
    );
    ASSERT_TRUE(cellHeadClear.valid());
    const Graphics::GpuTaskId counterClear = addInitializationClear(
        Name("tests/task_graph/surfel_initialize_counter_clear"),
        "Surfel GI Initialize Counter Clear",
        counter,
        cellHeadClear,
        chainedClearScheduling
    );
    ASSERT_TRUE(counterClear.valid());
    const Graphics::GpuTaskId freeListClear = addInitializationClear(
        Name("tests/task_graph/surfel_initialize_free_list_clear"),
        "Surfel GI Initialize Free-List Clear",
        freeList,
        counterClear,
        chainedClearScheduling
    );
    ASSERT_TRUE(freeListClear.valid());
    Graphics::GpuTaskDesc lifecycleDesc;
    lifecycleDesc
        .setIdentity(Name("tests/task_graph/surfel_initialize_lifecycle"))
        .setMarkerLabel("Surfel GI Initialize Lifecycle")
        .setQueue(computeTransferRequest)
        .setScheduling(chainedClearScheduling)
        .setDependencies(&freeListClear, 1u)
    ;
    const Graphics::GpuTaskId lifecycle = graph.addTask(lifecycleDesc);
    ASSERT_TRUE(lifecycle.valid());

    // Model the normal graph's immutable pool/cell snapshot on its distinct Transfer-preferred transport. Its
    // cross-queue RAW edges are the consumer frontier the clear/lifecycle chain must explicitly retain.
    const Graphics::GpuTaskResourceUse snapshotUses[] = {
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::CopySource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = poolSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::CopySource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = cellHeadsSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc snapshotDesc;
    snapshotDesc
        .setIdentity(Name("tests/task_graph/surfel_initialize_snapshot"))
        .setMarkerLabel("Surfel GI Snapshot")
        .setQueue(transferRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&lifecycle, 1u)
        .setResourceUses(snapshotUses, LengthOf(snapshotUses))
    ;
    const Graphics::GpuTaskId snapshot = graph.addTask(snapshotDesc);
    ASSERT_TRUE(snapshot.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prefix,
        poolClear,
        pool,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        poolClear,
        snapshot,
        pool,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        cellHeadClear,
        snapshot,
        cellHeads,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const poolClearAssignment = assignments.find(poolClear);
    ASSERT_NE(poolClearAssignment, nullptr);
    EXPECT_EQ(poolClearAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuTaskQueueAssignment* const snapshotAssignment = assignments.find(snapshot);
    ASSERT_NE(snapshotAssignment, nullptr);
    EXPECT_EQ(snapshotAssignment->queueClass, Graphics::CommandQueue::Transfer);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId initializePacket = compiledPlan.packetForTask(poolClear);
    const Graphics::GpuSubmissionPacketId snapshotPacket = compiledPlan.packetForTask(snapshot);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(initializePacket.valid());
    ASSERT_TRUE(snapshotPacket.valid());
    EXPECT_NE(prefixPacket, initializePacket);
    EXPECT_NE(initializePacket, snapshotPacket);
    EXPECT_EQ(compiledPlan.packetForTask(cellHeadClear), initializePacket);
    EXPECT_EQ(compiledPlan.packetForTask(counterClear), initializePacket);
    EXPECT_EQ(compiledPlan.packetForTask(freeListClear), initializePacket);
    EXPECT_EQ(compiledPlan.packetForTask(lifecycle), initializePacket);
    ASSERT_EQ(compiledPlan.packet(initializePacket).plan->taskCount, 5u);
    const Graphics::GpuTaskId* const initializeTasks = compiledPlan.packet(initializePacket).tasks;
    ASSERT_NE(initializeTasks, nullptr);
    EXPECT_EQ(initializeTasks[0u], poolClear);
    EXPECT_EQ(initializeTasks[1u], cellHeadClear);
    EXPECT_EQ(initializeTasks[2u], counterClear);
    EXPECT_EQ(initializeTasks[3u], freeListClear);
    EXPECT_EQ(initializeTasks[4u], lifecycle);
    ASSERT_EQ(compiledPlan.packet(snapshotPacket).plan->taskCount, 1u);
    EXPECT_EQ(compiledPlan.packet(snapshotPacket).tasks[0u], snapshot);

    const auto expectsInitializeTransition = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        ASSERT_NE(compiledTask, nullptr);
        const Graphics::GpuPacketStateSeed* const seeds = compiledPlan.findTask(task).prologueStateSeeds;
        ASSERT_NE(seeds, nullptr);
        bool seededFromPrefix = false;
        for(usize seedIndex = 0u; seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
            seededFromPrefix = seededFromPrefix
                || (
                    seeds[seedIndex].resource == resource
                    && seeds[seedIndex].sourcePacket == prefixPacket
                )
            ;
        }
        EXPECT_TRUE(seededFromPrefix);
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        ASSERT_NE(barriers, nullptr);
        bool transitioned = false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            transitioned = transitioned
                || (
                    barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                    && barrier.resource == resource
                    && barrier.before == Graphics::ResourceStates::UnorderedAccess
                    && barrier.after == Graphics::ResourceStates::CopyDest
                )
            ;
        }
        EXPECT_TRUE(transitioned);
    };
    expectsInitializeTransition(poolClear, pool);
    expectsInitializeTransition(cellHeadClear, cellHeads);
    expectsInitializeTransition(counterClear, counter);
    expectsInitializeTransition(freeListClear, freeList);
    ASSERT_EQ(compiledPlan.packet(initializePacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(initializePacket).dependencies[0u].producer, prefixPacket);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

