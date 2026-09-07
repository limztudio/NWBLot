// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_probes_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The CSG-only Extinction route uses the frozen interval-sample producer rather than the regular material plan.
// On a real Vulkan packet, receiver/cutter/clip uploads and all four removed-interval aliases must be ready before
// the producer opens Extinction timing; the immediate Extinction raster then observes the generated vertices as
// vertex buffers, closes that same timing span, and accepts with AVBOIT Pre's one Graphics submission token.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedUnsplitAvboitExtinctionCsgAliasFreeComputeEmulationStaysInPrePacket){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_UnsplitAvboitExtinctionLifecycleScope.identity, device, 1u));
    auto timingResetCommandList = device.createCommandList();
    ASSERT_NE(timingResetCommandList.get(), nullptr);
    timingResetCommandList->open();
    timing.recordFrameReset(*timingResetCommandList);
    timingResetCommandList->close();
    CommandList* timingResetCommandLists[] = { timingResetCommandList.get() };
    const QueueSubmissionToken timingResetToken = device.executeCommandLists(
        timingResetCommandLists,
        LengthOf(timingResetCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(timingResetToken.valid());
    timing.confirmFrameReset(timingResetToken);

    const auto createWorkBuffer = [&device](
        const Name& debugName,
        const bool isVertexBuffer = false,
        const bool isConstantBuffer = false
    ){
        BufferDesc description;
        description
            .setDebugName(debugName)
            .setByteSize(3u * 4u * sizeof(f32))
            .setStructStride(4u * sizeof(f32))
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
        ;
        if(isVertexBuffer)
            description.setIsVertexBuffer(true);
        if(isConstantBuffer)
            description.setIsConstantBuffer(true);
        return device.createBuffer(description);
    };
    const auto createStorageArray = [&device](const u32 arraySize){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setArraySize(arraySize)
                .setDimension(TextureDimension::Texture2DArray)
                .setFormat(Format::RGBA32_UINT)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
        );
    };

    auto preState = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_pre_state"),
        false,
        true
    );
    auto receiverRanges = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_receiver_ranges")
    );
    auto cutters = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_cutters"));
    auto clipContextSlots = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_clip_context_slots"),
        false,
        true
    );
    auto intervalSampleState = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_interval_sample_state"),
        false,
        true
    );
    auto generatedVertexA = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_generated_vertex_a"),
        true
    );
    auto generatedVertexB = createWorkBuffer(
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_generated_vertex_b"),
        true
    );
    auto extinction = createWorkBuffer(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_output"));
    auto removedIntervalDepth = createStorageArray(16u);
    auto removedIntervalCapNormal = createStorageArray(16u);
    auto removedIntervalData = createStorageArray(16u);
    auto removedIntervalCount = createStorageArray(1u);
    ASSERT_TRUE(
        preState
        && receiverRanges
        && cutters
        && clipContextSlots
        && intervalSampleState
        && generatedVertexA
        && generatedVertexB
        && extinction
        && removedIntervalDepth
        && removedIntervalCapNormal
        && removedIntervalData
        && removedIntervalCount
    );
    EXPECT_NE(generatedVertexA.get(), generatedVertexB.get());

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
    const auto importTexture = [&graph](const TextureHandle& texture, const Name& identity, const AStringView markerLabel){
        const TextureDesc& description = texture->getDescription();
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(description.initialState)
                .setQueueSharing(description.queueSharing)
        );
    };
    const GpuGraphResourceId preStateResource = importBuffer(preState, "AVBOIT Extinction CSG Pre State");
    const GpuGraphResourceId receiverRangesResource = importBuffer(
        receiverRanges,
        "AVBOIT Extinction CSG Receiver Ranges"
    );
    const GpuGraphResourceId cuttersResource = importBuffer(cutters, "AVBOIT Extinction CSG Cutters");
    const GpuGraphResourceId clipContextSlotsResource = importBuffer(
        clipContextSlots,
        "AVBOIT Extinction CSG Clip Context Slots"
    );
    const GpuGraphResourceId intervalSampleStateResource = importBuffer(
        intervalSampleState,
        "AVBOIT Extinction CSG Interval Sample State"
    );
    const GpuGraphResourceId generatedVertexAResource = importBuffer(
        generatedVertexA,
        "AVBOIT Extinction CSG Generated Vertex A"
    );
    const GpuGraphResourceId generatedVertexBResource = importBuffer(
        generatedVertexB,
        "AVBOIT Extinction CSG Generated Vertex B"
    );
    const GpuGraphResourceId extinctionResource = importBuffer(extinction, "AVBOIT Extinction");
    const GpuGraphResourceId removedIntervalDepthResource = importTexture(
        removedIntervalDepth,
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_removed_interval_depth"),
        "AVBOIT Extinction CSG Removed Interval Depth"
    );
    const GpuGraphResourceId removedIntervalCapNormalResource = importTexture(
        removedIntervalCapNormal,
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_removed_interval_cap_normal"),
        "AVBOIT Extinction CSG Removed Interval Cap Normal"
    );
    const GpuGraphResourceId removedIntervalDataResource = importTexture(
        removedIntervalData,
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_removed_interval_data"),
        "AVBOIT Extinction CSG Removed Interval Data"
    );
    const GpuGraphResourceId removedIntervalCountResource = importTexture(
        removedIntervalCount,
        Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_removed_interval_count"),
        "AVBOIT Extinction CSG Removed Interval Count"
    );
    ASSERT_TRUE(
        preStateResource.valid()
        && receiverRangesResource.valid()
        && cuttersResource.valid()
        && clipContextSlotsResource.valid()
        && intervalSampleStateResource.valid()
        && generatedVertexAResource.valid()
        && generatedVertexBResource.valid()
        && extinctionResource.valid()
        && removedIntervalDepthResource.valid()
        && removedIntervalCapNormalResource.valid()
        && removedIntervalDataResource.valid()
        && removedIntervalCountResource.valid()
    );
    EXPECT_NE(generatedVertexAResource, generatedVertexBResource);

    const GpuGraphResourceId generatedVertexOutputs[] = {
        generatedVertexAResource,
        generatedVertexBResource,
    };
    const GpuGraphResourceSetId generatedVertexOutputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_generated_vertex_outputs"))
            .setMarkerLabel("AVBOIT Extinction CSG Generated Vertex Outputs")
            .setMembers(generatedVertexOutputs, LengthOf(generatedVertexOutputs))
    );
    ASSERT_TRUE(generatedVertexOutputSet.valid());

    const TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse preUses[] = {
        GpuTaskResourceUse{
            .resource = preStateResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse combineUses[] = {
        GpuTaskResourceUse{
            .resource = preStateResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCapNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse receiverRangesUploadUses[] = {
        GpuTaskResourceUse{
            .resource = receiverRangesResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse cuttersUploadUses[] = {
        GpuTaskResourceUse{
            .resource = cuttersResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse clipContextSlotsUploadUses[] = {
        GpuTaskResourceUse{
            .resource = clipContextSlotsResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = receiverRangesResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = cuttersResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = clipContextSlotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = intervalSampleStateResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCapNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse extinctionUses[] = {
        GpuTaskResourceUse{
            .resource = receiverRangesResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = cuttersResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = clipContextSlotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = intervalSampleStateResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCapNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = extinctionResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceSetUse producerGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::UnorderedAccess,
        .access = GpuTaskResourceAccess::Write,
    };
    const GpuTaskResourceSetUse extinctionGeneratedVertexUse{
        .resourceSet = generatedVertexOutputSet,
        .range = {},
        .requiredState = ResourceStates::VertexBuffer,
        .access = GpuTaskResourceAccess::Read,
    };

    const GpuQueueRequest graphicsComputeQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = GpuTaskCostHint::Large;
    preScheduling.overlapPreferred = false;
    preScheduling.avoidQueueCrossing = true;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    preScheduling.mergeWithPrevious = false;
    GpuTaskSchedulingHint packetTailScheduling = preScheduling;
    packetTailScheduling.cost = GpuTaskCostHint::Medium;
    packetTailScheduling.mergeWithPrevious = true;
    packetTailScheduling.allowMergeAcrossConsumerFrontier = true;

    GpuTimingSubmissionTicket preTimingTicket(timing);
    Optional<GpuTimingMeasure> extinctionTiming;
    u32 recordOrdinal = 0u;
    bool preRecorded = false;
    bool combineRecorded = false;
    bool receiverRangesUploadRecorded = false;
    bool cuttersUploadRecorded = false;
    bool clipContextSlotsUploadRecorded = false;
    bool producerRecorded = false;
    bool extinctionRecorded = false;
    bool extinctionTimingStarted = false;
    bool extinctionTimingFinished = false;
    QueueSubmissionToken preAcceptedToken;
    QueueSubmissionToken combineAcceptedToken;
    QueueSubmissionToken receiverRangesUploadAcceptedToken;
    QueueSubmissionToken cuttersUploadAcceptedToken;
    QueueSubmissionToken clipContextSlotsUploadAcceptedToken;
    QueueSubmissionToken producerAcceptedToken;
    QueueSubmissionToken extinctionAcceptedToken;

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload prePayload;
    prePayload.expectations[0u] = { preState.get(), ResourceStates::ConstantBuffer };
    prePayload.expectationCount = 1u;
    prePayload.recordOrdinal = &recordOrdinal;
    prePayload.expectedOrdinal = 0u;
    prePayload.timingTicket = &preTimingTicket;
    prePayload.recorded = &preRecorded;
    prePayload.acceptedToken = &preAcceptedToken;
    const GpuTaskId preTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_pre_task"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(graphicsQueue)
            .setScheduling(preScheduling)
            .setResourceUses(preUses, LengthOf(preUses)),
        Move(prePayload)
    );
    ASSERT_TRUE(preTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload combinePayload;
    combinePayload.expectations[0u] = { preState.get(), ResourceStates::ConstantBuffer };
    combinePayload.expectationCount = 1u;
    combinePayload.textureExpectations[0u] = { removedIntervalDepth.get(), ResourceStates::UnorderedAccess };
    combinePayload.textureExpectations[1u] = { removedIntervalCapNormal.get(), ResourceStates::UnorderedAccess };
    combinePayload.textureExpectations[2u] = { removedIntervalData.get(), ResourceStates::UnorderedAccess };
    combinePayload.textureExpectations[3u] = { removedIntervalCount.get(), ResourceStates::UnorderedAccess };
    combinePayload.textureExpectationCount = 4u;
    combinePayload.recordOrdinal = &recordOrdinal;
    combinePayload.expectedOrdinal = 1u;
    combinePayload.timingTicket = &preTimingTicket;
    combinePayload.recorded = &combineRecorded;
    combinePayload.acceptedToken = &combineAcceptedToken;
    const GpuTaskId combineTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_combine_task"))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&preTask, 1u)
            .setResourceUses(combineUses, LengthOf(combineUses)),
        Move(combinePayload)
    );
    ASSERT_TRUE(combineTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload receiverRangesUploadPayload;
    receiverRangesUploadPayload.expectations[0u] = { receiverRanges.get(), ResourceStates::CopyDest };
    receiverRangesUploadPayload.expectationCount = 1u;
    receiverRangesUploadPayload.recordOrdinal = &recordOrdinal;
    receiverRangesUploadPayload.expectedOrdinal = 2u;
    receiverRangesUploadPayload.timingTicket = &preTimingTicket;
    receiverRangesUploadPayload.recorded = &receiverRangesUploadRecorded;
    receiverRangesUploadPayload.acceptedToken = &receiverRangesUploadAcceptedToken;
    const GpuTaskId receiverRangesUploadTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_receiver_ranges_upload_task"))
            .setMarkerLabel("AVBOIT Extinction CSG Receiver Ranges Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&combineTask, 1u)
            .setResourceUses(receiverRangesUploadUses, LengthOf(receiverRangesUploadUses)),
        Move(receiverRangesUploadPayload)
    );
    ASSERT_TRUE(receiverRangesUploadTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload cuttersUploadPayload;
    cuttersUploadPayload.expectations[0u] = { cutters.get(), ResourceStates::CopyDest };
    cuttersUploadPayload.expectationCount = 1u;
    cuttersUploadPayload.recordOrdinal = &recordOrdinal;
    cuttersUploadPayload.expectedOrdinal = 3u;
    cuttersUploadPayload.timingTicket = &preTimingTicket;
    cuttersUploadPayload.recorded = &cuttersUploadRecorded;
    cuttersUploadPayload.acceptedToken = &cuttersUploadAcceptedToken;
    const GpuTaskId cuttersUploadTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_cutters_upload_task"))
            .setMarkerLabel("AVBOIT Extinction CSG Cutters Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&receiverRangesUploadTask, 1u)
            .setResourceUses(cuttersUploadUses, LengthOf(cuttersUploadUses)),
        Move(cuttersUploadPayload)
    );
    ASSERT_TRUE(cuttersUploadTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload clipContextSlotsUploadPayload;
    clipContextSlotsUploadPayload.expectations[0u] = { clipContextSlots.get(), ResourceStates::CopyDest };
    clipContextSlotsUploadPayload.expectationCount = 1u;
    clipContextSlotsUploadPayload.recordOrdinal = &recordOrdinal;
    clipContextSlotsUploadPayload.expectedOrdinal = 4u;
    clipContextSlotsUploadPayload.timingTicket = &preTimingTicket;
    clipContextSlotsUploadPayload.recorded = &clipContextSlotsUploadRecorded;
    clipContextSlotsUploadPayload.acceptedToken = &clipContextSlotsUploadAcceptedToken;
    const GpuTaskId clipContextSlotsUploadTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_clip_context_slots_upload_task"))
            .setMarkerLabel("AVBOIT Extinction CSG Clip Context Slots Upload")
            .setQueue(graphicsQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&cuttersUploadTask, 1u)
            .setResourceUses(clipContextSlotsUploadUses, LengthOf(clipContextSlotsUploadUses)),
        Move(clipContextSlotsUploadPayload)
    );
    ASSERT_TRUE(clipContextSlotsUploadTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload producerPayload;
    producerPayload.expectations[0u] = { receiverRanges.get(), ResourceStates::ShaderResource };
    producerPayload.expectations[1u] = { cutters.get(), ResourceStates::ShaderResource };
    producerPayload.expectations[2u] = { clipContextSlots.get(), ResourceStates::ConstantBuffer };
    producerPayload.expectations[3u] = { intervalSampleState.get(), ResourceStates::ConstantBuffer };
    producerPayload.expectations[4u] = { generatedVertexA.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectations[5u] = { generatedVertexB.get(), ResourceStates::UnorderedAccess };
    producerPayload.expectationCount = 6u;
    producerPayload.textureExpectations[0u] = { removedIntervalDepth.get(), ResourceStates::UnorderedAccess };
    producerPayload.textureExpectations[1u] = { removedIntervalCapNormal.get(), ResourceStates::UnorderedAccess };
    producerPayload.textureExpectations[2u] = { removedIntervalData.get(), ResourceStates::UnorderedAccess };
    producerPayload.textureExpectations[3u] = { removedIntervalCount.get(), ResourceStates::UnorderedAccess };
    producerPayload.textureExpectationCount = 4u;
    producerPayload.recordOrdinal = &recordOrdinal;
    producerPayload.expectedOrdinal = 5u;
    producerPayload.device = &device;
    producerPayload.timing = &timing;
    producerPayload.timingTicket = &preTimingTicket;
    producerPayload.sharedTiming = &extinctionTiming;
    producerPayload.timingScope = &s_UnsplitAvboitExtinctionLifecycleScope;
    producerPayload.startTiming = true;
    producerPayload.timingStarted = &extinctionTimingStarted;
    producerPayload.recorded = &producerRecorded;
    producerPayload.acceptedToken = &producerAcceptedToken;
    const GpuTaskId producerTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_compute_emulation_task"))
            .setMarkerLabel("AVBOIT Extinction CSG Compute Emulation")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&clipContextSlotsUploadTask, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
            .setResourceSetUses(&producerGeneratedVertexUse, 1u),
        Move(producerPayload)
    );
    ASSERT_TRUE(producerTask.valid());

    NativePacketAsyncAvboitExtinctionLifecycleTask::Payload extinctionPayload;
    extinctionPayload.expectations[0u] = { receiverRanges.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[1u] = { cutters.get(), ResourceStates::ShaderResource };
    extinctionPayload.expectations[2u] = { clipContextSlots.get(), ResourceStates::ConstantBuffer };
    extinctionPayload.expectations[3u] = { intervalSampleState.get(), ResourceStates::ConstantBuffer };
    extinctionPayload.expectations[4u] = { generatedVertexA.get(), ResourceStates::VertexBuffer };
    extinctionPayload.expectations[5u] = { generatedVertexB.get(), ResourceStates::VertexBuffer };
    extinctionPayload.expectations[6u] = { extinction.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.expectationCount = 7u;
    extinctionPayload.textureExpectations[0u] = { removedIntervalDepth.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.textureExpectations[1u] = { removedIntervalCapNormal.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.textureExpectations[2u] = { removedIntervalData.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.textureExpectations[3u] = { removedIntervalCount.get(), ResourceStates::UnorderedAccess };
    extinctionPayload.textureExpectationCount = 4u;
    extinctionPayload.recordOrdinal = &recordOrdinal;
    extinctionPayload.expectedOrdinal = 6u;
    extinctionPayload.device = &device;
    extinctionPayload.timing = &timing;
    extinctionPayload.timingTicket = &preTimingTicket;
    extinctionPayload.sharedTiming = &extinctionTiming;
    extinctionPayload.timingScope = &s_UnsplitAvboitExtinctionLifecycleScope;
    extinctionPayload.finishTiming = true;
    extinctionPayload.timingFinished = &extinctionTimingFinished;
    extinctionPayload.recorded = &extinctionRecorded;
    extinctionPayload.acceptedToken = &extinctionAcceptedToken;
    const GpuTaskId extinctionTask = graph.addTask<NativePacketAsyncAvboitExtinctionLifecycleTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_raster_task"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(graphicsComputeQueue)
            .setScheduling(packetTailScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(extinctionUses, LengthOf(extinctionUses))
            .setResourceSetUses(&extinctionGeneratedVertexUse, 1u),
        Move(extinctionPayload)
    );
    ASSERT_TRUE(extinctionTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/unsplit_avboit_extinction_csg_lifecycle_scratch"));
    GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, frontierOptions));

    EXPECT_TRUE(analysis.hasExplicitEdge(preTask, combineTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(combineTask, receiverRangesUploadTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(receiverRangesUploadTask, cuttersUploadTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(cuttersUploadTask, clipContextSlotsUploadTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(clipContextSlotsUploadTask, producerTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(producerTask, extinctionTask));
    EXPECT_TRUE(analysis.hasInferredEdge(combineTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(combineTask, extinctionTask));
    EXPECT_TRUE(analysis.hasInferredEdge(receiverRangesUploadTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(cuttersUploadTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(clipContextSlotsUploadTask, producerTask));
    EXPECT_TRUE(analysis.hasInferredEdge(producerTask, extinctionTask));
    const GpuTaskId tasks[] = {
        preTask,
        combineTask,
        receiverRangesUploadTask,
        cuttersUploadTask,
        clipContextSlotsUploadTask,
        producerTask,
        extinctionTask,
    };
    ASSERT_EQ(analysis.topologicalOrder().size(), LengthOf(tasks));
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);
    for(const GpuTaskId task : tasks){
        const GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queue, primaryGraphicsQueue);
        EXPECT_EQ(assignment->queueClass, CommandQueue::Graphics);
    }

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(preTask);
    ASSERT_TRUE(packet.valid());
    for(const GpuTaskId task : tasks)
        EXPECT_EQ(views.compiled.packetForTask(task), packet);
    EXPECT_TRUE(views.compiled.tasksSharePacket(producerTask, extinctionTask));
    EXPECT_TRUE(views.compiled.taskPrecedesOrSharesPacket(producerTask, extinctionTask));
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    EXPECT_EQ(packetPlan.plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(packetPlan.plan->dependencyCount, 0u);
    ASSERT_EQ(packetPlan.plan->taskCount, LengthOf(tasks));
    const GpuTaskId* const packetTasks = views.compiled.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(tasks); ++taskIndex)
        EXPECT_EQ(packetTasks[taskIndex], tasks[taskIndex]);
    EXPECT_EQ(packetTasks[5u], producerTask);
    EXPECT_EQ(packetTasks[6u], extinctionTask);

    const auto hasBufferTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
                && barrier.sourceQueue == primaryGraphicsQueue
                && barrier.destinationQueue == primaryGraphicsQueue
            )
                return true;
        }
        return false;
    };
    const auto hasTextureUav = [&](const GpuTaskId task, const GpuGraphResourceId resource, const TextureSubresourceSet& range){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        for(u32 barrierIndex = 0u; compiledTask.valid() && barriers && barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == ResourceStates::UnorderedAccess
                && barrier.after == ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasBufferTransition(
        receiverRangesUploadTask,
        receiverRangesResource,
        ResourceStates::Common,
        ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        cuttersUploadTask,
        cuttersResource,
        ResourceStates::Common,
        ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        clipContextSlotsUploadTask,
        clipContextSlotsResource,
        ResourceStates::Common,
        ResourceStates::CopyDest
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        receiverRangesResource,
        ResourceStates::CopyDest,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        cuttersResource,
        ResourceStates::CopyDest,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        clipContextSlotsResource,
        ResourceStates::CopyDest,
        ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasBufferTransition(
        producerTask,
        intervalSampleStateResource,
        ResourceStates::Common,
        ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasTextureUav(producerTask, removedIntervalDepthResource, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producerTask, removedIntervalCapNormalResource, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producerTask, removedIntervalDataResource, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(producerTask, removedIntervalCountResource, removedIntervalCountRange));
    for(const GpuGraphResourceId output : generatedVertexOutputs){
        EXPECT_TRUE(hasBufferTransition(
            producerTask,
            output,
            ResourceStates::Common,
            ResourceStates::UnorderedAccess
        ));
        EXPECT_TRUE(hasBufferTransition(
            extinctionTask,
            output,
            ResourceStates::UnorderedAccess,
            ResourceStates::VertexBuffer
        ));
    }
    EXPECT_TRUE(hasBufferTransition(
        extinctionTask,
        extinctionResource,
        ResourceStates::Common,
        ResourceStates::UnorderedAccess
    ));

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
    EXPECT_EQ(recordOrdinal, LengthOf(tasks));
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(combineRecorded);
    EXPECT_TRUE(receiverRangesUploadRecorded);
    EXPECT_TRUE(cuttersUploadRecorded);
    EXPECT_TRUE(clipContextSlotsUploadRecorded);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(extinctionRecorded);
    EXPECT_TRUE(extinctionTimingStarted);
    EXPECT_TRUE(extinctionTimingFinished);
    EXPECT_FALSE(extinctionTiming.has_value());
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, preTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto finalStateProbe = device.createCommandList();
    ASSERT_NE(finalStateProbe.get(), nullptr);
    finalStateProbe->open(finalState);
    EXPECT_EQ(finalStateProbe->getBufferState(receiverRanges.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(cutters.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateProbe->getBufferState(clipContextSlots.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(intervalSampleState.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexA.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(generatedVertexB.get()), ResourceStates::VertexBuffer);
    EXPECT_EQ(finalStateProbe->getBufferState(extinction.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(
        finalStateProbe->getTextureSubresourceState(removedIntervalDepth.get(), 0u, 0u),
        ResourceStates::UnorderedAccess
    );
    EXPECT_EQ(
        finalStateProbe->getTextureSubresourceState(removedIntervalCapNormal.get(), 0u, 0u),
        ResourceStates::UnorderedAccess
    );
    EXPECT_EQ(
        finalStateProbe->getTextureSubresourceState(removedIntervalData.get(), 0u, 0u),
        ResourceStates::UnorderedAccess
    );
    EXPECT_EQ(
        finalStateProbe->getTextureSubresourceState(removedIntervalCount.get(), 0u, 0u),
        ResourceStates::UnorderedAccess
    );
    finalStateProbe->close();

    // AVBOIT Pre remains the sole submit binding.  Both lifecycle callbacks must share its single accepted
    // Graphics token, which proves the producer/raster timing span cannot cross a packet boundary.
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{ .task = preTask, .timingTicket = &preTimingTicket },
    };
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        preTask,
        extinctionTask,
        nullptr,
        0u,
        timingTickets,
        LengthOf(timingTickets),
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_EQ(packetToken.queue, CommandQueue::Graphics);
    EXPECT_EQ(packetToken.physicalQueueIndex, primaryGraphicsQueue.index);
    const auto expectPacketToken = [&](const QueueSubmissionToken& token){
        ASSERT_TRUE(token.valid());
        EXPECT_EQ(token.queue, packetToken.queue);
        EXPECT_EQ(token.value, packetToken.value);
        EXPECT_EQ(token.physicalQueueIndex, packetToken.physicalQueueIndex);
        EXPECT_EQ(token.deviceGeneration, packetToken.deviceGeneration);
    };
    for(const GpuTaskId task : tasks)
        expectPacketToken(transaction.taskToken(views.compiled, task));
    expectPacketToken(preAcceptedToken);
    expectPacketToken(combineAcceptedToken);
    expectPacketToken(receiverRangesUploadAcceptedToken);
    expectPacketToken(cuttersUploadAcceptedToken);
    expectPacketToken(clipContextSlotsUploadAcceptedToken);
    expectPacketToken(producerAcceptedToken);
    expectPacketToken(extinctionAcceptedToken);

    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    const auto timingStats = timingSink.stats(s_UnsplitAvboitExtinctionLifecycleScope.identity);
    ASSERT_TRUE(timingStats.valid());
    EXPECT_EQ(timingStats.sampleCount, 1u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

