// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Pure-software prepared mesh builds graph-own every typed sentinel clear. A rebuild clears sort keys, parent
// links, and the shared visit counter; a following refit clears that counter again only after the rebuild callback
// has consumed it. Shadow Preparation then consumes the exact frozen scene/material traversal inputs. The native
// callbacks here are state probes, so the capture proves the real built-ins and values while the compiler owns every
// CopyDest -> UAV / terminal SRV handoff without a callback-local scene-table transition.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedPureSoftwareBvhAndSceneTraversalRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeBuildStateBuffer = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const BufferHandle position = makeBuildStateBuffer();
    const BufferHandle index = makeBuildStateBuffer();
    const BufferHandle node = makeBuildStateBuffer();
    const BufferHandle parent = makeBuildStateBuffer();
    const BufferHandle sortKeys = makeBuildStateBuffer();
    const BufferHandle sortPayload = makeBuildStateBuffer();
    const BufferHandle visitCounter = makeBuildStateBuffer();
    const BufferHandle sceneNodes = makeBuildStateBuffer();
    const BufferHandle sceneInstances = makeBuildStateBuffer();
    const BufferHandle instanceMaterials = makeBuildStateBuffer();
    const BufferHandle shadowInstances = makeBuildStateBuffer();
    const BufferHandle materialTyped = makeBuildStateBuffer();
    ASSERT_NE(position.get(), nullptr);
    ASSERT_NE(index.get(), nullptr);
    ASSERT_NE(node.get(), nullptr);
    ASSERT_NE(parent.get(), nullptr);
    ASSERT_NE(sortKeys.get(), nullptr);
    ASSERT_NE(sortPayload.get(), nullptr);
    ASSERT_NE(visitCounter.get(), nullptr);
    ASSERT_NE(sceneNodes.get(), nullptr);
    ASSERT_NE(sceneInstances.get(), nullptr);
    ASSERT_NE(instanceMaterials.get(), nullptr);
    ASSERT_NE(shadowInstances.get(), nullptr);
    ASSERT_NE(materialTyped.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuildState = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId positionResource = importBuildState(
        position,
        Name("tests/descriptor_buffer/pure_sw_bvh_position"),
        "Pure SW-BVH Position"
    );
    const GpuGraphResourceId indexResource = importBuildState(
        index,
        Name("tests/descriptor_buffer/pure_sw_bvh_index"),
        "Pure SW-BVH Index"
    );
    const GpuGraphResourceId nodeResource = importBuildState(
        node,
        Name("tests/descriptor_buffer/pure_sw_bvh_node"),
        "Pure SW-BVH Node"
    );
    const GpuGraphResourceId parentResource = importBuildState(
        parent,
        Name("tests/descriptor_buffer/pure_sw_bvh_parent"),
        "Pure SW-BVH Parent"
    );
    const GpuGraphResourceId sortKeysResource = importBuildState(
        sortKeys,
        Name("tests/descriptor_buffer/pure_sw_bvh_sort_keys"),
        "Pure SW-BVH Sort Keys"
    );
    const GpuGraphResourceId sortPayloadResource = importBuildState(
        sortPayload,
        Name("tests/descriptor_buffer/pure_sw_bvh_sort_payload"),
        "Pure SW-BVH Sort Payload"
    );
    const GpuGraphResourceId visitCounterResource = importBuildState(
        visitCounter,
        Name("tests/descriptor_buffer/pure_sw_bvh_visit_counter"),
        "Pure SW-BVH Visit Counter"
    );
    const GpuGraphResourceId sceneNodesResource = importBuildState(
        sceneNodes,
        Name("tests/descriptor_buffer/pure_sw_scene_nodes"),
        "Pure SW Scene Nodes"
    );
    const GpuGraphResourceId sceneInstancesResource = importBuildState(
        sceneInstances,
        Name("tests/descriptor_buffer/pure_sw_scene_instances"),
        "Pure SW Scene Instances"
    );
    const GpuGraphResourceId instanceMaterialsResource = importBuildState(
        instanceMaterials,
        Name("tests/descriptor_buffer/pure_sw_instance_materials"),
        "Pure SW Instance Materials"
    );
    const GpuGraphResourceId shadowInstancesResource = importBuildState(
        shadowInstances,
        Name("tests/descriptor_buffer/pure_sw_shadow_instances"),
        "Pure SW Shadow Instances"
    );
    const GpuGraphResourceId materialTypedResource = importBuildState(
        materialTyped,
        Name("tests/descriptor_buffer/pure_sw_material_typed"),
        "Pure SW Typed Materials"
    );
    ASSERT_TRUE(positionResource.valid());
    ASSERT_TRUE(indexResource.valid());
    ASSERT_TRUE(nodeResource.valid());
    ASSERT_TRUE(parentResource.valid());
    ASSERT_TRUE(sortKeysResource.valid());
    ASSERT_TRUE(sortPayloadResource.valid());
    ASSERT_TRUE(visitCounterResource.valid());
    ASSERT_TRUE(sceneNodesResource.valid());
    ASSERT_TRUE(sceneInstancesResource.valid());
    ASSERT_TRUE(instanceMaterialsResource.valid());
    ASSERT_TRUE(shadowInstancesResource.valid());
    ASSERT_TRUE(materialTypedResource.valid());

    const GpuQueueRequest clearQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest buildQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.allowPacketMerge = true;
    const auto addClear = [&](
        const Name identity,
        const AStringView label,
        const GpuGraphResourceId destination,
        const u32 value,
        const GpuTaskId* dependencies,
        const usize dependencyCount,
        const bool mergeWithPrevious
    ){
        GpuTaskSchedulingHint scheduling = clearScheduling;
        scheduling.mergeWithPrevious = mergeWithPrevious;
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(clearQueue)
            .setScheduling(scheduling)
            .setDependencies(dependencies, dependencyCount)
        ;
        return graph.addClearBufferTask(
            desc,
            GpuClearBufferTaskDesc{
                .destination = destination,
                .clearValue = value,
            }
        );
    };
    constexpr u32 invalidNode = Limit<u32>::s_Max;
    const GpuTaskId rebuildKeysClear = addClear(
        Name("tests/descriptor_buffer/pure_sw_bvh_rebuild_keys_clear"),
        "Pure SW-BVH Rebuild Keys Clear",
        sortKeysResource,
        invalidNode,
        nullptr,
        0u,
        false
    );
    ASSERT_TRUE(rebuildKeysClear.valid());
    const GpuTaskId rebuildParentClear = addClear(
        Name("tests/descriptor_buffer/pure_sw_bvh_rebuild_parent_clear"),
        "Pure SW-BVH Rebuild Parent Clear",
        parentResource,
        invalidNode,
        &rebuildKeysClear,
        1u,
        true
    );
    ASSERT_TRUE(rebuildParentClear.valid());
    const GpuTaskId rebuildCounterClear = addClear(
        Name("tests/descriptor_buffer/pure_sw_bvh_rebuild_counter_clear"),
        "Pure SW-BVH Rebuild Counter Clear",
        visitCounterResource,
        0u,
        &rebuildParentClear,
        1u,
        true
    );
    ASSERT_TRUE(rebuildCounterClear.valid());

    const GpuTaskResourceUse buildUses[] = {
        { .resource = positionResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = indexResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = nodeResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = parentResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = sortKeysResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = sortPayloadResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = visitCounterResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = sceneNodesResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = sceneInstancesResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = instanceMaterialsResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = shadowInstancesResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
        { .resource = materialTypedResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskSchedulingHint buildScheduling;
    buildScheduling.cost = GpuTaskCostHint::Large;
    buildScheduling.allowPacketMerge = true;
    buildScheduling.mergeWithPrevious = true;
    GpuTaskDesc rebuildDesc;
    rebuildDesc
        .setIdentity(Name("tests/descriptor_buffer/pure_sw_bvh_rebuild"))
        .setMarkerLabel("Pure SW-BVH Rebuild")
        .setQueue(buildQueue)
        .setScheduling(buildScheduling)
        .setDependencies(&rebuildCounterClear, 1u)
        .setResourceUses(buildUses, LengthOf(buildUses))
    ;
    bool rebuildRecorded = false;
    const GpuTaskId rebuild = graph.addTask<NativePacketPrefixTask>(
        rebuildDesc,
        NativePacketPrefixTask::Payload{
            .buffer = sortKeys.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .additionalBuffer = parent.get(),
            .expectedAdditionalBufferState = ResourceStates::UnorderedAccess,
            .recorded = &rebuildRecorded,
        }
    );
    ASSERT_TRUE(rebuild.valid());
    const GpuTaskId refitCounterClear = addClear(
        Name("tests/descriptor_buffer/pure_sw_bvh_refit_counter_clear"),
        "Pure SW-BVH Refit Counter Clear",
        visitCounterResource,
        0u,
        &rebuild,
        1u,
        true
    );
    ASSERT_TRUE(refitCounterClear.valid());
    GpuTaskDesc refitDesc;
    refitDesc
        .setIdentity(Name("tests/descriptor_buffer/pure_sw_bvh_refit"))
        .setMarkerLabel("Pure SW-BVH Refit")
        .setQueue(buildQueue)
        .setScheduling(buildScheduling)
        .setDependencies(&refitCounterClear, 1u)
        .setResourceUses(buildUses, LengthOf(buildUses))
    ;
    bool refitRecorded = false;
    const GpuTaskId refit = graph.addTask<NativePacketPrefixTask>(
        refitDesc,
        NativePacketPrefixTask::Payload{
            .buffer = visitCounter.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &refitRecorded,
        }
    );
    ASSERT_TRUE(refit.valid());

    const GpuTaskResourceUse shadowPrepareUses[] = {
        { .resource = nodeResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = parentResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = sortKeysResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = sortPayloadResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = visitCounterResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
    };
    GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/descriptor_buffer/pure_sw_bvh_shadow_prepare"))
        .setMarkerLabel("Pure SW-BVH Shadow Preparation")
        .setQueue(buildQueue)
        .setScheduling(buildScheduling)
        .setDependencies(&refit, 1u)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    bool shadowPrepareRecorded = false;
    QueueSubmissionToken acceptedToken;
    const GpuTaskId shadowPrepare = graph.addTask<NativePacketPrefixTask>(
        shadowPrepareDesc,
        NativePacketPrefixTask::Payload{
            .buffer = node.get(),
            .expectedState = ResourceStates::ShaderResource,
            .additionalBuffer = sceneNodes.get(),
            .expectedAdditionalBufferState = ResourceStates::ShaderResource,
            .recorded = &shadowPrepareRecorded,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(shadowPrepare.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/pure_sw_bvh_sentinel_chain_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(shadowPrepare);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(rebuildKeysClear), packet);
    EXPECT_EQ(views.compiled.packetForTask(rebuildParentClear), packet);
    EXPECT_EQ(views.compiled.packetForTask(rebuildCounterClear), packet);
    EXPECT_EQ(views.compiled.packetForTask(rebuild), packet);
    EXPECT_EQ(views.compiled.packetForTask(refitCounterClear), packet);
    EXPECT_EQ(views.compiled.packetForTask(refit), packet);
    const GpuTaskId expectedTasks[] = {
        rebuildKeysClear,
        rebuildParentClear,
        rebuildCounterClear,
        rebuild,
        refitCounterClear,
        refit,
        shadowPrepare,
    };
    const GpuCompiledPacketView packetDesc = views.compiled.packet(packet);
    ASSERT_TRUE(packetDesc.valid());
    ASSERT_EQ(packetDesc.plan->taskCount, LengthOf(expectedTasks));
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(expectedTasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], expectedTasks[taskIndex]);

    const auto hasTransition = [&](
        const GpuTaskId task,
        const GpuGraphResourceId resource,
        const ResourceStates::Mask before,
        const ResourceStates::Mask after
    ){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(!compiledTask.valid() || (compiledTask.plan->prologueBarrierCount != 0u && !barriers))
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(rebuild, sortKeysResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, parentResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, visitCounterResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, nodeResource, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(rebuild, sortPayloadResource, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(refitCounterClear, visitCounterResource, ResourceStates::UnorderedAccess, ResourceStates::CopyDest));
    EXPECT_TRUE(hasTransition(refit, visitCounterResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransition(shadowPrepare, nodeResource, ResourceStates::UnorderedAccess, ResourceStates::ShaderResource));
    const auto hasUavBarrier = [&](const GpuTaskId task, const GpuGraphResourceId resource){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(!compiledTask.valid() || (compiledTask.plan->prologueBarrierCount != 0u && !barriers))
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferUav
                && barrier.resource == resource
                && barrier.before == ResourceStates::UnorderedAccess
                && barrier.after == ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, parentResource));
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, sortKeysResource));
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, sortPayloadResource));
    EXPECT_TRUE(hasUavBarrier(shadowPrepare, visitCounterResource));
    EXPECT_TRUE(hasUavBarrier(refit, nodeResource));
    EXPECT_TRUE(hasUavBarrier(refit, parentResource));
    EXPECT_TRUE(hasUavBarrier(refit, sortKeysResource));
    EXPECT_TRUE(hasUavBarrier(refit, sortPayloadResource));

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket,
        &commandIrCapture
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(rebuildRecorded);
    EXPECT_TRUE(refitRecorded);
    EXPECT_TRUE(shadowPrepareRecorded);
    ASSERT_EQ(commandIrCapture.recordCount(), 4u);
    const GpuTaskId expectedClearTasks[] = {
        rebuildKeysClear,
        rebuildParentClear,
        rebuildCounterClear,
        refitCounterClear,
    };
    const u32 expectedClearValues[] = { invalidNode, invalidNode, 0u, 0u };
    for(usize recordIndex = 0u; recordIndex < LengthOf(expectedClearTasks); ++recordIndex){
        const GpuCommandIrBuiltinTaskRecord* const clearRecord = commandIrCapture.recordAt(recordIndex);
        ASSERT_NE(clearRecord, nullptr);
        EXPECT_EQ(clearRecord->opcode, GpuCommandIrOpcode::ClearBuffer);
        EXPECT_EQ(clearRecord->task, expectedClearTasks[recordIndex]);
        EXPECT_EQ(clearRecord->packet, packet);
        EXPECT_EQ(clearRecord->uintClearValue, UIntColor(expectedClearValues[recordIndex]));
    }
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, shadowPrepare, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(node.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(parent.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(stateProbe->getBufferState(sortKeys.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(stateProbe->getBufferState(sortPayload.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(stateProbe->getBufferState(visitCounter.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(stateProbe->getBufferState(sceneNodes.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(sceneInstances.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(instanceMaterials.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(shadowInstances.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(materialTyped.get()), ResourceStates::ShaderResource);
    stateProbe->close();

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(acceptedToken.valid());
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_EQ(acceptedToken.queue, packetToken.queue);
    EXPECT_EQ(acceptedToken.value, packetToken.value);
    EXPECT_EQ(acceptedToken.physicalQueueIndex, packetToken.physicalQueueIndex);
    EXPECT_EQ(acceptedToken.deviceGeneration, packetToken.deviceGeneration);
    EXPECT_TRUE(device.waitForIdle());
}


// Prepared BLAS work with no software-BVH tail in the aggregate Shadow Preparation callback has its frozen
// position/index inputs observe graph-owned AccelStructBuildInput before recording, and the graph-owned
// normalizer publishes ShaderResource afterward without either callback issuing a native state change.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedPreparedTailFreeBlasInputStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AccelStructBuildInput state: ray tracing acceleration structures are not enabled on this device.";

    const auto makeInputBuffer = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsAccelStructBuildInput(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const BufferHandle position = makeInputBuffer();
    const BufferHandle index = makeInputBuffer();
    ASSERT_NE(position.get(), nullptr);
    ASSERT_NE(index.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importInput = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId positionResource = importInput(
        position,
        Name("tests/descriptor_buffer/prepared_tail_free_blas_position"),
        "Prepared Tail-Free BLAS Position"
    );
    const GpuGraphResourceId indexResource = importInput(
        index,
        Name("tests/descriptor_buffer/prepared_tail_free_blas_index"),
        "Prepared Tail-Free BLAS Index"
    );
    ASSERT_TRUE(positionResource.valid());
    ASSERT_TRUE(indexResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint prepareScheduling;
    prepareScheduling.cost = GpuTaskCostHint::Medium;
    prepareScheduling.forceSubmissionBoundary = false;
    prepareScheduling.allowPacketMerge = true;
    const GpuTaskResourceUse prepareUses[] = {
        {
            .resource = positionResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructBuildInput,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = indexResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructBuildInput,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/descriptor_buffer/prepared_tail_free_blas_shadow_prepare"))
        .setMarkerLabel("Prepared Tail-Free BLAS Shadow Preparation")
        .setQueue(graphicsQueue)
        .setScheduling(prepareScheduling)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    bool prepareRecorded = false;
    const GpuTaskId prepareTask = graph.addTask<NativePacketPrefixTask>(
        prepareDesc,
        NativePacketPrefixTask::Payload{
            .buffer = position.get(),
            .expectedState = ResourceStates::AccelStructBuildInput,
            .additionalBuffer = index.get(),
            .expectedAdditionalBufferState = ResourceStates::AccelStructBuildInput,
            .recorded = &prepareRecorded,
        }
    );
    ASSERT_TRUE(prepareTask.valid());

    GpuTaskSchedulingHint normalizeScheduling = prepareScheduling;
    normalizeScheduling.cost = GpuTaskCostHint::Tiny;
    normalizeScheduling.mergeWithPrevious = true;
    const GpuTaskResourceUse normalizeUses[] = {
        {
            .resource = positionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = indexResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/descriptor_buffer/prepared_tail_free_blas_normalize"))
        .setMarkerLabel("Prepared Tail-Free BLAS Normalize")
        .setQueue(graphicsQueue)
        .setScheduling(normalizeScheduling)
        .setDependencies(&prepareTask, 1u)
        .setResourceUses(normalizeUses, LengthOf(normalizeUses))
    ;
    bool normalizeRecorded = false;
    const GpuTaskId normalizeTask = graph.addTask<NativePacketPrefixTask>(
        normalizeDesc,
        NativePacketPrefixTask::Payload{
            .buffer = position.get(),
            .expectedState = ResourceStates::ShaderResource,
            .additionalBuffer = index.get(),
            .expectedAdditionalBufferState = ResourceStates::ShaderResource,
            .recorded = &normalizeRecorded,
        }
    );
    ASSERT_TRUE(normalizeTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/prepared_tail_free_blas_input_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(prepareTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(normalizeTask), packet);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 2u);

    const GpuCompiledTaskView compiledPrepare = views.compiled.findTask(prepareTask);
    const GpuCompiledTaskView compiledNormalize = views.compiled.findTask(normalizeTask);
    ASSERT_TRUE(compiledPrepare.valid());
    ASSERT_TRUE(compiledNormalize.valid());
    const auto hasTransition = [&](const GpuTaskId task, const GpuCompiledTask& compiledTask, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(compiledTask.prologueBarrierCount != 0u && !barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(
        prepareTask,
        *compiledPrepare.plan,
        positionResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        prepareTask,
        *compiledPrepare.plan,
        indexResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        normalizeTask,
        *compiledNormalize.plan,
        positionResource,
        ResourceStates::AccelStructBuildInput,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTransition(
        normalizeTask,
        *compiledNormalize.plan,
        indexResource,
        ResourceStates::AccelStructBuildInput,
        ResourceStates::ShaderResource
    ));

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(prepareRecorded);
    EXPECT_TRUE(normalizeRecorded);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, prepareTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;

    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(position.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(index.get()), ResourceStates::ShaderResource);
    stateProbe->close();

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

