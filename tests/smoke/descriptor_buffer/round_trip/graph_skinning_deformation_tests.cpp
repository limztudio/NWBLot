// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_skinning_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Active-pose skinning must not rely on a native UAV/SRV bridge between deformation and bounds/repack. This native
// packet smoke models the exact three graph tasks: deformation writes skinned position/normal/tangent, post-dispatch
// reads position/normal while writing bounds/attributes, and the finalizer publishes every generated buffer as SRV.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSkinningDeformationHandoffStaysInGraph){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto createGeneratedBuffer = [&device](const Name& debugName){
        return device.createBuffer(
            BufferDesc()
                .setDebugName(debugName)
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
        );
    };
    auto skinnedPosition = createGeneratedBuffer(Name("tests/descriptor_buffer/skinning_stage_position"));
    auto skinnedNormal = createGeneratedBuffer(Name("tests/descriptor_buffer/skinning_stage_normal"));
    auto skinnedTangent = createGeneratedBuffer(Name("tests/descriptor_buffer/skinning_stage_tangent"));
    auto meshletBounds = createGeneratedBuffer(Name("tests/descriptor_buffer/skinning_stage_bounds"));
    auto attributes = createGeneratedBuffer(Name("tests/descriptor_buffer/skinning_stage_attributes"));
    ASSERT_NE(skinnedPosition.get(), nullptr);
    ASSERT_NE(skinnedNormal.get(), nullptr);
    ASSERT_NE(skinnedTangent.get(), nullptr);
    ASSERT_NE(meshletBounds.get(), nullptr);
    ASSERT_NE(attributes.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const AStringView markerLabel){
        const BufferDesc& description = buffer->getDescription();
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(description.debugName)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(description.initialState)
                .setQueueSharing(description.queueSharing)
        );
    };
    const GpuGraphResourceId skinnedPositionResource = importBuffer(skinnedPosition, "Skinning Stage Position");
    const GpuGraphResourceId skinnedNormalResource = importBuffer(skinnedNormal, "Skinning Stage Normal");
    const GpuGraphResourceId skinnedTangentResource = importBuffer(skinnedTangent, "Skinning Stage Tangent");
    const GpuGraphResourceId meshletBoundsResource = importBuffer(meshletBounds, "Skinning Stage Bounds");
    const GpuGraphResourceId attributesResource = importBuffer(attributes, "Skinning Stage Attributes");
    ASSERT_TRUE(skinnedPositionResource.valid());
    ASSERT_TRUE(skinnedNormalResource.valid());
    ASSERT_TRUE(skinnedTangentResource.valid());
    ASSERT_TRUE(meshletBoundsResource.valid());
    ASSERT_TRUE(attributesResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint deformationScheduling;
    deformationScheduling.cost = GpuTaskCostHint::Small;
    deformationScheduling.overlapPreferred = false;
    deformationScheduling.avoidQueueCrossing = true;
    deformationScheduling.forceSubmissionBoundary = false;
    deformationScheduling.allowPacketMerge = true;
    deformationScheduling.frontierScoredMergeDomain = Name("tests/descriptor_buffer/skinning_stage_handoff");
    const GpuTaskResourceUse deformationUses[] = {
        GpuTaskResourceUse{
            .resource = skinnedPositionResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = skinnedNormalResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = skinnedTangentResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool deformationObservedUavs = false;
    SkinningGraphStateProbeTask::Payload deformationPayload;
    deformationPayload.expectations[0u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedPositionResource,
        .state = ResourceStates::UnorderedAccess,
    };
    deformationPayload.expectations[1u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedNormalResource,
        .state = ResourceStates::UnorderedAccess,
    };
    deformationPayload.expectations[2u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedTangentResource,
        .state = ResourceStates::UnorderedAccess,
    };
    deformationPayload.expectationCount = 3u;
    deformationPayload.recorded = &deformationObservedUavs;
    const GpuTaskId deformationTask = graph.addTask<SkinningGraphStateProbeTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_stage_deformation"))
            .setMarkerLabel("Skinning Deformation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(deformationScheduling)
            .setResourceUses(deformationUses, LengthOf(deformationUses)),
        Move(deformationPayload)
    );
    ASSERT_TRUE(deformationTask.valid());

    const GpuTaskResourceUse postDispatchUses[] = {
        GpuTaskResourceUse{
            .resource = skinnedPositionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = skinnedNormalResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = meshletBoundsResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = attributesResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool postDispatchObservedStates = false;
    SkinningGraphStateProbeTask::Payload postDispatchPayload;
    postDispatchPayload.expectations[0u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedPositionResource,
        .state = ResourceStates::ShaderResource,
    };
    postDispatchPayload.expectations[1u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedNormalResource,
        .state = ResourceStates::ShaderResource,
    };
    postDispatchPayload.expectations[2u] = SkinningGraphStateProbeTask::Expectation{
        .resource = meshletBoundsResource,
        .state = ResourceStates::UnorderedAccess,
    };
    postDispatchPayload.expectations[3u] = SkinningGraphStateProbeTask::Expectation{
        .resource = attributesResource,
        .state = ResourceStates::UnorderedAccess,
    };
    postDispatchPayload.expectationCount = 4u;
    postDispatchPayload.recorded = &postDispatchObservedStates;
    const GpuTaskId postDispatchTask = graph.addTask<SkinningGraphStateProbeTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_stage_bounds_repack"))
            .setMarkerLabel("Skinning Bounds and Repack")
            .setQueue(graphicsComputeQueue)
            .setScheduling(deformationScheduling)
            .setDependencies(&deformationTask, 1u)
            .setResourceUses(postDispatchUses, LengthOf(postDispatchUses)),
        Move(postDispatchPayload)
    );
    ASSERT_TRUE(postDispatchTask.valid());

    const GpuTaskResourceUse finalizerUses[] = {
        GpuTaskResourceUse{
            .resource = skinnedPositionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = skinnedNormalResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = skinnedTangentResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = meshletBoundsResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = attributesResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool finalizerObservedShaderResources = false;
    QueueSubmissionToken finalizerAcceptedToken;
    SkinningGraphStateProbeTask::Payload finalizerPayload;
    finalizerPayload.expectations[0u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedPositionResource,
        .state = ResourceStates::ShaderResource,
    };
    finalizerPayload.expectations[1u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedNormalResource,
        .state = ResourceStates::ShaderResource,
    };
    finalizerPayload.expectations[2u] = SkinningGraphStateProbeTask::Expectation{
        .resource = skinnedTangentResource,
        .state = ResourceStates::ShaderResource,
    };
    finalizerPayload.expectations[3u] = SkinningGraphStateProbeTask::Expectation{
        .resource = meshletBoundsResource,
        .state = ResourceStates::ShaderResource,
    };
    finalizerPayload.expectations[4u] = SkinningGraphStateProbeTask::Expectation{
        .resource = attributesResource,
        .state = ResourceStates::ShaderResource,
    };
    finalizerPayload.expectationCount = 5u;
    finalizerPayload.recorded = &finalizerObservedShaderResources;
    finalizerPayload.acceptedToken = &finalizerAcceptedToken;
    const GpuTaskId finalizerTask = graph.addTask<SkinningGraphStateProbeTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_stage_finalize"))
            .setMarkerLabel("Skinning Finalize States")
            .setQueue(graphicsComputeQueue)
            .setScheduling(deformationScheduling)
            .setDependencies(&postDispatchTask, 1u)
            .setResourceUses(finalizerUses, LengthOf(finalizerUses)),
        Move(finalizerPayload)
    );
    ASSERT_TRUE(finalizerTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/skinning_stage_handoff_scratch"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierScored;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    const GpuTaskQueueAssignment* const deformationAssignment = assignments.find(deformationTask);
    const GpuTaskQueueAssignment* const postDispatchAssignment = assignments.find(postDispatchTask);
    const GpuTaskQueueAssignment* const finalizerAssignment = assignments.find(finalizerTask);
    ASSERT_NE(deformationAssignment, nullptr);
    ASSERT_NE(postDispatchAssignment, nullptr);
    ASSERT_NE(finalizerAssignment, nullptr);
    EXPECT_EQ(deformationAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(postDispatchAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(finalizerAssignment->queue, primaryGraphicsQueue);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId deformationPacket = views.compiled.packetForTask(deformationTask);
    const GpuSubmissionPacketId postDispatchPacket = views.compiled.packetForTask(postDispatchTask);
    const GpuSubmissionPacketId finalizerPacket = views.compiled.packetForTask(finalizerTask);
    ASSERT_TRUE(deformationPacket.valid());
    ASSERT_TRUE(postDispatchPacket.valid());
    ASSERT_TRUE(finalizerPacket.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(postDispatchPacket, deformationPacket);
    EXPECT_EQ(finalizerPacket, deformationPacket);
    EXPECT_EQ(views.compiled.packet(deformationPacket).plan->taskCount, 3u);
    EXPECT_EQ(
        views.compiled.packetizationDecisionForTask(postDispatchTask),
        GpuTaskPacketizationDecision::MergedFrontierScored
    );
    EXPECT_EQ(
        views.compiled.packetizationDecisionForTask(finalizerTask),
        GpuTaskPacketizationDecision::MergedFrontierScored
    );

    const GpuCompiledTaskView compiledDeformation = views.compiled.findTask(deformationTask);
    const GpuCompiledTaskView compiledPostDispatch = views.compiled.findTask(postDispatchTask);
    const GpuCompiledTaskView compiledFinalizer = views.compiled.findTask(finalizerTask);
    ASSERT_TRUE(compiledDeformation.valid());
    ASSERT_TRUE(compiledPostDispatch.valid());
    ASSERT_TRUE(compiledFinalizer.valid());
    EXPECT_EQ(compiledDeformation.plan->prologueBarrierCount, 3u);
    EXPECT_EQ(compiledPostDispatch.plan->prologueBarrierCount, 4u);
    EXPECT_EQ(compiledFinalizer.plan->prologueBarrierCount, 3u);
    const GpuCompiledBarrier* const postDispatchBarriers = views.compiled.findTask(postDispatchTask).prologueBarriers;
    const GpuCompiledBarrier* const finalizerBarriers = views.compiled.findTask(finalizerTask).prologueBarriers;
    ASSERT_NE(postDispatchBarriers, nullptr);
    ASSERT_NE(finalizerBarriers, nullptr);
    EXPECT_EQ(postDispatchBarriers[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(postDispatchBarriers[0u].resource, skinnedPositionResource);
    EXPECT_EQ(postDispatchBarriers[0u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(postDispatchBarriers[0u].after, ResourceStates::ShaderResource);
    EXPECT_EQ(postDispatchBarriers[1u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(postDispatchBarriers[1u].resource, skinnedNormalResource);
    EXPECT_EQ(postDispatchBarriers[1u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(postDispatchBarriers[1u].after, ResourceStates::ShaderResource);
    EXPECT_EQ(finalizerBarriers[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(finalizerBarriers[0u].resource, skinnedTangentResource);
    EXPECT_EQ(finalizerBarriers[0u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalizerBarriers[0u].after, ResourceStates::ShaderResource);
    EXPECT_EQ(finalizerBarriers[1u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(finalizerBarriers[1u].resource, meshletBoundsResource);
    EXPECT_EQ(finalizerBarriers[1u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalizerBarriers[1u].after, ResourceStates::ShaderResource);
    EXPECT_EQ(finalizerBarriers[2u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(finalizerBarriers[2u].resource, attributesResource);
    EXPECT_EQ(finalizerBarriers[2u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalizerBarriers[2u].after, ResourceStates::ShaderResource);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_TRUE(deformationObservedUavs);
    EXPECT_TRUE(postDispatchObservedStates);
    EXPECT_TRUE(finalizerObservedShaderResources);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, deformationTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(skinnedPosition.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(skinnedNormal.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(skinnedTangent.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(meshletBounds.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(attributes.get()), ResourceStates::ShaderResource);
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
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
    const QueueSubmissionToken packetToken = transaction.packetToken(deformationPacket);
    ASSERT_TRUE(packetToken.valid());
    ASSERT_TRUE(finalizerAcceptedToken.valid());
    EXPECT_EQ(finalizerAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(finalizerAcceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

