// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_geometry_preparation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, PlansGraphOwnedPreparedSoftwareBvhInputStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId position = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_sw_bvh_position"),
        "Prepared SW-BVH Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId index = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_sw_bvh_index"),
        "Prepared SW-BVH Index",
        Graphics::ResourceStates::AccelStructBuildInput,
        queueSharing
    );
    ASSERT_TRUE(position.valid());
    ASSERT_TRUE(index.valid());

    const Graphics::GpuGraphResourceId traceGeometryMembers[] = { position, index };
    const Graphics::GpuGraphResourceSetId traceGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/prepared_sw_bvh_trace_geometry"))
            .setMarkerLabel("Prepared SW-BVH Trace Geometry")
            .setMembers(traceGeometryMembers, LengthOf(traceGeometryMembers))
    );
    ASSERT_TRUE(traceGeometrySet.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    scheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceSetUse traceGeometrySetUses[] = {
        {
            .resourceSet = traceGeometrySet,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/prepared_sw_bvh_shadow_prepare"))
        .setMarkerLabel("Prepared Software BVH Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceSetUses(traceGeometrySetUses, LengthOf(traceGeometrySetUses))
    ;
    const Graphics::GpuTaskId prepare = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepare.valid());

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

    ASSERT_EQ(compiledPlan.packetCount(), 1u);

    const Graphics::GpuCompiledTask* const compiledPrepare = compiledPlan.findTask(prepare).plan;
    ASSERT_NE(compiledPrepare, nullptr);
    const Graphics::GpuCompiledBarrier* const prepareBarriers = compiledPlan.findTask(prepare).prologueBarriers;
    ASSERT_NE(prepareBarriers, nullptr);
    const auto hasInputTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(usize barrierIndex = 0u; barrierIndex < compiledPrepare->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = prepareBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasInputTransition(position, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasInputTransition(index, Graphics::ResourceStates::AccelStructBuildInput));
}

TEST(GpuTaskGraph, MergesPreparedPureSoftwareBvhAndSceneTraversalIntoShadowPreparePacket){
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
    const Graphics::GpuGraphResourceId position = addBuffer(
        Name("tests/task_graph/pure_sw_bvh_position"),
        "Pure SW-BVH Position"
    );
    const Graphics::GpuGraphResourceId index = addBuffer(
        Name("tests/task_graph/pure_sw_bvh_index"),
        "Pure SW-BVH Index"
    );
    const Graphics::GpuGraphResourceId node = addBuffer(
        Name("tests/task_graph/pure_sw_bvh_node"),
        "Pure SW-BVH Node"
    );
    const Graphics::GpuGraphResourceId parent = addBuffer(
        Name("tests/task_graph/pure_sw_bvh_parent"),
        "Pure SW-BVH Parent"
    );
    const Graphics::GpuGraphResourceId sortKeys = addBuffer(
        Name("tests/task_graph/pure_sw_bvh_sort_keys"),
        "Pure SW-BVH Sort Keys"
    );
    const Graphics::GpuGraphResourceId sortPayload = addBuffer(
        Name("tests/task_graph/pure_sw_bvh_sort_payload"),
        "Pure SW-BVH Sort Payload"
    );
    const Graphics::GpuGraphResourceId visitCounter = addBuffer(
        Name("tests/task_graph/pure_sw_bvh_visit_counter"),
        "Pure SW-BVH Visit Counter"
    );
    const Graphics::GpuGraphResourceId sceneNodes = addBuffer(
        Name("tests/task_graph/pure_sw_scene_nodes"),
        "Pure SW Scene Nodes"
    );
    const Graphics::GpuGraphResourceId sceneInstances = addBuffer(
        Name("tests/task_graph/pure_sw_scene_instances"),
        "Pure SW Scene Instances"
    );
    const Graphics::GpuGraphResourceId instanceMaterials = addBuffer(
        Name("tests/task_graph/pure_sw_instance_materials"),
        "Pure SW Instance Materials"
    );
    const Graphics::GpuGraphResourceId shadowInstances = addBuffer(
        Name("tests/task_graph/pure_sw_shadow_instances"),
        "Pure SW Shadow Instances"
    );
    const Graphics::GpuGraphResourceId materialTyped = addBuffer(
        Name("tests/task_graph/pure_sw_material_typed"),
        "Pure SW Typed Materials"
    );
    ASSERT_TRUE(position.valid());
    ASSERT_TRUE(index.valid());
    ASSERT_TRUE(node.valid());
    ASSERT_TRUE(parent.valid());
    ASSERT_TRUE(sortKeys.valid());
    ASSERT_TRUE(sortPayload.valid());
    ASSERT_TRUE(visitCounter.valid());
    ASSERT_TRUE(sceneNodes.valid());
    ASSERT_TRUE(sceneInstances.valid());
    ASSERT_TRUE(instanceMaterials.valid());
    ASSERT_TRUE(shadowInstances.valid());
    ASSERT_TRUE(materialTyped.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsUploadRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Graphics,
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
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };

    Graphics::GpuTaskSchedulingHint preflightScheduling;
    preflightScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    preflightScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc preflightDesc;
    preflightDesc
        .setIdentity(Name("tests/task_graph/pure_sw_bvh_preflight"))
        .setMarkerLabel("Pure SW-BVH Preflight")
        .setQueue(graphicsRequest)
        .setScheduling(preflightScheduling)
    ;
    const Graphics::GpuTaskId preflight = graph.addTask(preflightDesc);
    ASSERT_TRUE(preflight.valid());

    Graphics::GpuTaskSchedulingHint chainedScheduling;
    chainedScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    chainedScheduling.allowPacketMerge = true;
    chainedScheduling.mergeWithPrevious = true;
    chainedScheduling.allowMergeAcrossConsumerFrontier = true;
    const auto addClear = [&](
        const Name identity,
        const AStringView label,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::GpuTaskId dependency
    ){
        const Graphics::GpuTaskResourceUse clearUses[] = {
            {
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::CopyDest,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        Graphics::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsUploadRequest)
            .setScheduling(chainedScheduling)
            .setDependencies(&dependency, 1u)
            .setResourceUses(clearUses, LengthOf(clearUses))
        ;
        return graph.addTask(clearDesc);
    };
    const auto addBuild = [&](const Name identity, const AStringView label, const Graphics::GpuTaskId dependency){
        const Graphics::GpuTaskResourceUse buildUses[] = {
            { .resource = position, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
            { .resource = index, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
            { .resource = node, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
            { .resource = parent, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
            { .resource = sortKeys, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
            { .resource = sortPayload, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
            { .resource = visitCounter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        };
        Graphics::GpuTaskSchedulingHint buildScheduling = chainedScheduling;
        buildScheduling.cost = Graphics::GpuTaskCostHint::Large;
        Graphics::GpuTaskDesc buildDesc;
        buildDesc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsComputeRequest)
            .setScheduling(buildScheduling)
            .setDependencies(&dependency, 1u)
            .setResourceUses(buildUses, LengthOf(buildUses))
        ;
        return graph.addTask(buildDesc);
    };

    const Graphics::GpuTaskId rebuildKeysClear = addClear(
        Name("tests/task_graph/pure_sw_bvh_rebuild_keys_clear"),
        "Pure SW-BVH Rebuild Keys Clear",
        sortKeys,
        preflight
    );
    ASSERT_TRUE(rebuildKeysClear.valid());
    const Graphics::GpuTaskId rebuildParentClear = addClear(
        Name("tests/task_graph/pure_sw_bvh_rebuild_parent_clear"),
        "Pure SW-BVH Rebuild Parent Clear",
        parent,
        rebuildKeysClear
    );
    ASSERT_TRUE(rebuildParentClear.valid());
    const Graphics::GpuTaskId rebuildCounterClear = addClear(
        Name("tests/task_graph/pure_sw_bvh_rebuild_counter_clear"),
        "Pure SW-BVH Rebuild Counter Clear",
        visitCounter,
        rebuildParentClear
    );
    ASSERT_TRUE(rebuildCounterClear.valid());
    const Graphics::GpuTaskId rebuild = addBuild(
        Name("tests/task_graph/pure_sw_bvh_rebuild"),
        "Pure SW-BVH Rebuild",
        rebuildCounterClear
    );
    ASSERT_TRUE(rebuild.valid());
    const Graphics::GpuTaskId refitCounterClear = addClear(
        Name("tests/task_graph/pure_sw_bvh_refit_counter_clear"),
        "Pure SW-BVH Refit Counter Clear",
        visitCounter,
        rebuild
    );
    ASSERT_TRUE(refitCounterClear.valid());
    const Graphics::GpuTaskId refit = addBuild(
        Name("tests/task_graph/pure_sw_bvh_refit"),
        "Pure SW-BVH Refit",
        refitCounterClear
    );
    ASSERT_TRUE(refit.valid());

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        { .resource = node, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = parent, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = sortKeys, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = sortPayload, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = visitCounter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = sceneNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = instanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = shadowInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = materialTyped, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint shadowPrepareScheduling = chainedScheduling;
    shadowPrepareScheduling.cost = Graphics::GpuTaskCostHint::Large;
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/pure_sw_bvh_shadow_prepare"))
        .setMarkerLabel("Pure SW-BVH Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareScheduling)
        .setDependencies(&refit, 1u)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    const Graphics::GpuTaskResourceUse traversalUses[] = {
        { .resource = node, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    Graphics::GpuTaskSchedulingHint traversalScheduling;
    traversalScheduling.cost = Graphics::GpuTaskCostHint::Large;
    traversalScheduling.forceSubmissionBoundary = true;
    traversalScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc traversalDesc;
    traversalDesc
        .setIdentity(Name("tests/task_graph/pure_sw_bvh_traversal"))
        .setMarkerLabel("Pure SW-BVH Traversal")
        .setQueue(computeRequest)
        .setScheduling(traversalScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(traversalUses, LengthOf(traversalUses))
    ;
    const Graphics::GpuTaskId traversal = graph.addTask(traversalDesc);
    ASSERT_TRUE(traversal.valid());

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

    ASSERT_EQ(compiledPlan.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledPlan.packetForTask(shadowPrepare);
    ASSERT_TRUE(shadowPreparePacket.valid());
    EXPECT_EQ(compiledPlan.packetForTask(rebuildKeysClear), shadowPreparePacket);
    EXPECT_EQ(compiledPlan.packetForTask(rebuildParentClear), shadowPreparePacket);
    EXPECT_EQ(compiledPlan.packetForTask(rebuildCounterClear), shadowPreparePacket);
    EXPECT_EQ(compiledPlan.packetForTask(rebuild), shadowPreparePacket);
    EXPECT_EQ(compiledPlan.packetForTask(refitCounterClear), shadowPreparePacket);
    EXPECT_EQ(compiledPlan.packetForTask(refit), shadowPreparePacket);
    EXPECT_NE(compiledPlan.packetForTask(traversal), shadowPreparePacket);
    const Graphics::GpuTaskId expectedPacketTasks[] = {
        preflight,
        rebuildKeysClear,
        rebuildParentClear,
        rebuildCounterClear,
        rebuild,
        refitCounterClear,
        refit,
        shadowPrepare,
    };
    const Graphics::GpuSubmissionPacket& packet = *compiledPlan.packet(shadowPreparePacket).plan;
    ASSERT_EQ(packet.taskCount, LengthOf(expectedPacketTasks));
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(shadowPreparePacket).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(expectedPacketTasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], expectedPacketTasks[taskIndex]);

    const auto hasTransition = [&](
        const Graphics::GpuTaskId task,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!compiledTask || (compiledTask->prologueBarrierCount != 0u && !barriers))
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
    EXPECT_TRUE(hasTransition(rebuild, sortKeys, Graphics::ResourceStates::CopyDest, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, parent, Graphics::ResourceStates::CopyDest, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, visitCounter, Graphics::ResourceStates::CopyDest, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, node, Graphics::ResourceStates::Common, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, sortPayload, Graphics::ResourceStates::Common, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(refitCounterClear, visitCounter, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::CopyDest));
    EXPECT_TRUE(hasTransition(refit, visitCounter, Graphics::ResourceStates::CopyDest, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(shadowPrepare, node, Graphics::ResourceStates::UnorderedAccess, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(shadowPrepare, sceneNodes, Graphics::ResourceStates::Common, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(shadowPrepare, sceneInstances, Graphics::ResourceStates::Common, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(shadowPrepare, instanceMaterials, Graphics::ResourceStates::Common, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(shadowPrepare, shadowInstances, Graphics::ResourceStates::Common, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(shadowPrepare, materialTyped, Graphics::ResourceStates::Common, Graphics::ResourceStates::ShaderResource));
    const auto hasUavBarrier = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!compiledTask || (compiledTask->prologueBarrierCount != 0u && !barriers))
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferUav
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, parent));
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, sortKeys));
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, sortPayload));
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, visitCounter));
    EXPECT_TRUE(hasUavBarrier(refit, node));
    EXPECT_TRUE(hasUavBarrier(refit, parent));
    EXPECT_TRUE(hasUavBarrier(refit, sortKeys));
    EXPECT_TRUE(hasUavBarrier(refit, sortPayload));
}

TEST(GpuTaskGraph, PlansGraphOwnedPreparedTailFreeBlasInputStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId position = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_tail_free_blas_position"),
        "Prepared Tail-Free BLAS Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId index = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_tail_free_blas_index"),
        "Prepared Tail-Free BLAS Index",
        Graphics::ResourceStates::ShaderResource,
        queueSharing
    );
    ASSERT_TRUE(position.valid());
    ASSERT_TRUE(index.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint prepareScheduling;
    prepareScheduling.cost = Graphics::GpuTaskCostHint::Large;
    prepareScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse prepareUses[] = {
        {
            .resource = position,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructBuildInput,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = index,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructBuildInput,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/prepared_tail_free_blas_shadow_prepare"))
        .setMarkerLabel("Prepared Tail-Free BLAS Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(prepareScheduling)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    const Graphics::GpuTaskId prepare = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepare.valid());

    Graphics::GpuTaskSchedulingHint normalizeScheduling = prepareScheduling;
    normalizeScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    normalizeScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse normalizeUses[] = {
        {
            .resource = position,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = index,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/task_graph/prepared_tail_free_blas_normalize"))
        .setMarkerLabel("Prepared Tail-Free BLAS Normalize")
        .setQueue(graphicsRequest)
        .setScheduling(normalizeScheduling)
        .setDependencies(&prepare, 1u)
        .setResourceUses(normalizeUses, LengthOf(normalizeUses))
    ;
    const Graphics::GpuTaskId normalize = graph.addTask(normalizeDesc);
    ASSERT_TRUE(normalize.valid());

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

    ASSERT_EQ(compiledPlan.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(prepare);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packetForTask(normalize), packet);
    EXPECT_EQ(compiledPlan.packet(packet).plan->taskCount, 2u);

    const Graphics::GpuCompiledTask* const compiledPrepare = compiledPlan.findTask(prepare).plan;
    const Graphics::GpuCompiledTask* const compiledNormalize = compiledPlan.findTask(normalize).plan;
    ASSERT_NE(compiledPrepare, nullptr);
    ASSERT_NE(compiledNormalize, nullptr);
    const auto hasTransition = [&](
        const Graphics::GpuTaskId task,
        const Graphics::GpuCompiledTask& compiledTask,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(compiledTask.prologueBarrierCount != 0u && !barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.prologueBarrierCount; ++barrierIndex){
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
    EXPECT_TRUE(hasTransition(
        prepare,
        *compiledPrepare,
        position,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        prepare,
        *compiledPrepare,
        index,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        normalize,
        *compiledNormalize,
        position,
        Graphics::ResourceStates::AccelStructBuildInput,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTransition(
        normalize,
        *compiledNormalize,
        index,
        Graphics::ResourceStates::AccelStructBuildInput,
        Graphics::ResourceStates::ShaderResource
    ));
}

TEST(GpuTaskGraph, PlansGraphOwnedPostGbufferTraceGeometryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId rasterGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/post_gbuffer_raster_geometry"),
        "Post-G-Buffer Raster Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareBvh = AddBufferMetadata(
        graph,
        Name("tests/task_graph/post_gbuffer_software_bvh"),
        "Post-G-Buffer Software BVH",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(rasterGeometry.valid());
    ASSERT_TRUE(softwareBvh.valid());

    const Graphics::GpuGraphResourceId traceGeometryMembers[] = { rasterGeometry, softwareBvh };
    const Graphics::GpuGraphResourceSetId traceGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/post_gbuffer_trace_geometry"))
            .setMarkerLabel("Post-G-Buffer Trace Geometry")
            .setMembers(traceGeometryMembers, LengthOf(traceGeometryMembers))
    );
    ASSERT_TRUE(traceGeometrySet.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.allowPacketMerge = true;

    const Graphics::GpuTaskResourceUse prepareUses[] = {
        {
            .resource = rasterGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = softwareBvh,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/post_gbuffer_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    const Graphics::GpuTaskId prepare = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepare.valid());

    Graphics::GpuTaskSchedulingHint gbufferScheduling = scheduling;
    gbufferScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse gbufferUses[] = {
        {
            .resource = rasterGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = softwareBvh,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("tests/task_graph/post_gbuffer_gbuffer"))
        .setMarkerLabel("G-Buffer")
        .setQueue(graphicsRequest)
        .setScheduling(gbufferScheduling)
        .setDependencies(&prepare, 1u)
        .setResourceUses(gbufferUses, LengthOf(gbufferUses))
    ;
    const Graphics::GpuTaskId gbuffer = graph.addTask(gbufferDesc);
    ASSERT_TRUE(gbuffer.valid());

    const Graphics::GpuTaskResourceSetUse traceGeometrySetUses[] = {
        {
            .resourceSet = traceGeometrySet,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/task_graph/post_gbuffer_normalize_trace_geometry"))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setQueue(graphicsRequest)
        .setScheduling(gbufferScheduling)
        .setDependencies(&gbuffer, 1u)
        .setResourceSetUses(traceGeometrySetUses, LengthOf(traceGeometrySetUses))
    ;
    const Graphics::GpuTaskId normalize = graph.addTask(normalizeDesc);
    ASSERT_TRUE(normalize.valid());

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

    const Graphics::GpuCompiledTask* const compiledNormalize = compiledPlan.findTask(normalize).plan;
    ASSERT_NE(compiledNormalize, nullptr);
    const Graphics::GpuCompiledBarrier* const normalizeBarriers = compiledPlan.findTask(normalize).prologueBarriers;
    ASSERT_NE(normalizeBarriers, nullptr);
    const auto hasNormalizeBarrier = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(usize barrierIndex = 0u; barrierIndex < compiledNormalize->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = normalizeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasNormalizeBarrier(rasterGeometry, Graphics::ResourceStates::VertexBuffer));
    EXPECT_TRUE(hasNormalizeBarrier(softwareBvh, Graphics::ResourceStates::UnorderedAccess));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

