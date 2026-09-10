// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The UI compatibility path owns neither a renderer graph nor a native preparation list. This compact probe uses
// the public standalone submission boundary directly: an immutable upload must reach ShaderResource before the
// terminal callback accepts, while a rejected packet must discard that callback without publishing a token.
struct StandaloneGraphTextureUploadCompletionTask{
    struct Payload{
        GpuGraphResourceId texture;
        bool* recorded = nullptr;
        bool* accepted = nullptr;
        bool* discarded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        Texture* const texture = context.declarations.textureForResource(payload.texture);
        const bool ready = texture
            && commandList.getTextureSubresourceState(texture, 0u, 0u) == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.accepted)
            *payload.accepted = token.valid();
    }

    static void discarded(Payload& payload){
        if(payload.discarded)
            *payload.discarded = true;
    }
};


struct StandaloneGraphTextureUploadContext{
    TextureHandle destination;
    const void* bytes = nullptr;
    usize byteCount = 0u;
    usize rowPitch = 0u;
    bool* recorded = nullptr;
    bool* accepted = nullptr;
    bool* discarded = nullptr;
};


[[nodiscard]] static GpuTaskId DeclareStandaloneGraphTextureUpload(
    void* const rawContext,
    GpuTaskGraph& graph
){
    StandaloneGraphTextureUploadContext* const context =
        static_cast<StandaloneGraphTextureUploadContext*>(rawContext)
    ;
    if(!context || !context->destination || !context->bytes || context->byteCount == 0u)
        return {};

    const TextureDesc& textureDesc = context->destination->getDescription();
    const GpuGraphResourceId destination = graph.importTexture(
        context->destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests.standalone_graph_texture_upload.destination"))
            .setMarkerLabel("Standalone Graph Texture Upload")
            .setType(GpuGraphResourceType::Texture)
            // This probe creates the destination immediately before its graph-owned first write. The descriptor
            // state is the retained post-upload contract, while the native Vulkan image still starts Undefined.
            .setInitialState(ResourceStates::Unknown)
            .setQueueSharing(textureDesc.queueSharing)
    );
    const GpuUploadBlobId source = graph.copyUploadData(context->bytes, context->byteCount, alignof(u32));
    if(!destination.valid() || !source.valid())
        return {};

    GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = GpuTaskCostHint::Tiny;
    uploadScheduling.forceSubmissionBoundary = true;
    uploadScheduling.allowPacketMerge = false;
    GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests.standalone_graph_texture_upload.upload"))
        .setMarkerLabel("Standalone Graph Texture Upload")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(uploadScheduling)
    ;
    const GpuTaskId uploadTask = graph.addUploadTextureTask(
        uploadDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destination,
            .arraySlice = 0u,
            .mipLevel = 0u,
            .rowPitch = context->rowPitch,
            .depthPitch = 0u,
            .finalState = ResourceStates::ShaderResource,
        }
    );
    if(!uploadTask.valid())
        return {};

    const GpuTaskResourceUse completionUses[] = {
        GpuTaskResourceUse{
            .resource = destination,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint completionScheduling;
    completionScheduling.cost = GpuTaskCostHint::Tiny;
    completionScheduling.forceSubmissionBoundary = true;
    completionScheduling.allowPacketMerge = false;
    GpuTaskDesc completionDesc;
    completionDesc
        .setIdentity(Name("tests.standalone_graph_texture_upload.completion"))
        .setMarkerLabel("Standalone Graph Texture Upload Completion")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(completionScheduling)
        .setDependencies(&uploadTask, 1u)
        .setResourceUses(completionUses, LengthOf(completionUses))
    ;
    return graph.addTask<StandaloneGraphTextureUploadCompletionTask>(
        completionDesc,
        StandaloneGraphTextureUploadCompletionTask::Payload{
            .texture = destination,
            .recorded = context->recorded,
            .accepted = context->accepted,
            .discarded = context->discarded,
        }
    );
}


// Public standalone declarations may opt into the same compiler-derived worker frontiers as renderer-owned graph
// tasks. This probe keeps both caller arrays mutable so the built-in uploads prove graph-owned blob retention, while
// two side-effect-free packet tasks prove the same ready frontier receives nonzero worker-affined leases.
struct StandaloneGraphReadyFrontierObserverTask{
    struct Payload{
        Latch* recordingStarted = nullptr;
        u32* observedWorkerIndex = nullptr;
        bool* discarded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.recordingStarted || !payload.observedWorkerIndex)
            return false;

        *payload.observedWorkerIndex = commandList.getDescription().recordingWorkerIndex;
        payload.recordingStarted->count_down();
        payload.recordingStarted->wait();
        return commandList.isRecording();
    }

    static void discarded(Payload& payload){
        if(payload.discarded)
            *payload.discarded = true;
    }
};


struct StandaloneGraphReadyFrontierUploadContext{
    BufferHandle firstDestination;
    BufferHandle secondDestination;
    void* firstBytes = nullptr;
    void* secondBytes = nullptr;
    usize firstByteCount = 0u;
    usize secondByteCount = 0u;
    Latch* recordingStarted = nullptr;
    u32* firstWorkerIndex = nullptr;
    u32* secondWorkerIndex = nullptr;
    QueueSubmissionToken* firstAcceptedToken = nullptr;
    QueueSubmissionToken* secondAcceptedToken = nullptr;
    bool* firstDiscarded = nullptr;
    bool* secondDiscarded = nullptr;
};


[[nodiscard]] static GpuTaskId DeclareStandaloneGraphReadyFrontierUploads(
    void* const rawContext,
    GpuTaskGraph& graph
){
    StandaloneGraphReadyFrontierUploadContext* const context =
        static_cast<StandaloneGraphReadyFrontierUploadContext*>(rawContext)
    ;
    if(
        !context
        || !context->firstDestination
        || !context->secondDestination
        || !context->firstBytes
        || !context->secondBytes
        || context->firstByteCount == 0u
        || context->secondByteCount == 0u
        || !context->recordingStarted
        || !context->firstWorkerIndex
        || !context->secondWorkerIndex
    )
        return {};

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;
    const auto addUpload = [&graph, scheduling](
        const BufferHandle& destinationBuffer,
        void* const bytes,
        const usize byteCount,
        const Name& resourceIdentity,
        const AStringView resourceLabel,
        const Name& taskIdentity,
        const AStringView taskLabel,
        QueueSubmissionToken* const acceptedToken
    ){
        const GpuGraphResourceId destination = graph.importBuffer(
            destinationBuffer,
            GpuGraphResourceDesc{}
                .setIdentity(resourceIdentity)
                .setMarkerLabel(resourceLabel)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(destinationBuffer->getDescription().queueSharing)
        );
        const GpuUploadBlobId source = graph.copyUploadData(bytes, byteCount, alignof(u32));
        if(!destination.valid() || !source.valid())
            return GpuTaskId{};

        GpuTaskDesc desc;
        desc
            .setIdentity(taskIdentity)
            .setMarkerLabel(taskLabel)
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling)
        ;
        return graph.addUploadBufferTask(
            desc,
            GpuUploadBufferTaskDesc{
                .source = source,
                .destination = destination,
                .finalState = ResourceStates::CopyDest,
                .acceptedToken = acceptedToken,
            }
        );
    };

    const GpuTaskId firstUpload = addUpload(
        context->firstDestination,
        context->firstBytes,
        context->firstByteCount,
        Name("tests.standalone_graph_ready_frontier.first_destination"),
        "Standalone Ready Frontier First Destination",
        Name("tests.standalone_graph_ready_frontier.first_upload"),
        "Standalone Ready Frontier First Upload",
        context->firstAcceptedToken
    );
    const GpuTaskId secondUpload = addUpload(
        context->secondDestination,
        context->secondBytes,
        context->secondByteCount,
        Name("tests.standalone_graph_ready_frontier.second_destination"),
        "Standalone Ready Frontier Second Destination",
        Name("tests.standalone_graph_ready_frontier.second_upload"),
        "Standalone Ready Frontier Second Upload",
        context->secondAcceptedToken
    );
    if(!firstUpload.valid() || !secondUpload.valid())
        return {};

    const auto addObserver = [&graph, context, scheduling](
        const Name& identity,
        const AStringView label,
        u32* const observedWorkerIndex,
        bool* const discarded
    ){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling)
        ;
        return graph.addTask<StandaloneGraphReadyFrontierObserverTask>(
            desc,
            StandaloneGraphReadyFrontierObserverTask::Payload{
                .recordingStarted = context->recordingStarted,
                .observedWorkerIndex = observedWorkerIndex,
                .discarded = discarded,
            }
        );
    };
    const GpuTaskId firstObserver = addObserver(
        Name("tests.standalone_graph_ready_frontier.first_observer"),
        "Standalone Ready Frontier First Observer",
        context->firstWorkerIndex,
        context->firstDiscarded
    );
    const GpuTaskId secondObserver = addObserver(
        Name("tests.standalone_graph_ready_frontier.second_observer"),
        "Standalone Ready Frontier Second Observer",
        context->secondWorkerIndex,
        context->secondDiscarded
    );
    if(!firstObserver.valid() || !secondObserver.valid())
        return {};

    NWB_MEMSET(context->firstBytes, 0, context->firstByteCount);
    NWB_MEMSET(context->secondBytes, 0, context->secondByteCount);
    return secondObserver;
}


// Decoded/static textures carry multiple mip/slice payloads, so they cannot use the one-subresource setup helper.
// The batch primitive must retain each CPU payload as a graph blob, serialize the subresource uploads through one
// terminal acceptance point, and publish ShaderResource state for every mip before a legacy Graphics consumer runs.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedTextureUploadBatchCopiesMipsAndPublishesTerminalToken){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();

    u8 mip0Bytes[4u * 4u * 4u];
    u8 mip1Bytes[2u * 2u * 4u];
    for(usize byteIndex = 0u; byteIndex < sizeof(mip0Bytes); ++byteIndex)
        mip0Bytes[byteIndex] = static_cast<u8>(byteIndex * 13u + 7u);
    for(usize byteIndex = 0u; byteIndex < sizeof(mip1Bytes); ++byteIndex)
        mip1Bytes[byteIndex] = static_cast<u8>(byteIndex * 29u + 3u);
    u8 expectedMip0[sizeof(mip0Bytes)];
    u8 expectedMip1[sizeof(mip1Bytes)];
    NWB_MEMCPY(expectedMip0, sizeof(expectedMip0), mip0Bytes, sizeof(mip0Bytes));
    NWB_MEMCPY(expectedMip1, sizeof(expectedMip1), mip1Bytes, sizeof(mip1Bytes));

    const TextureHandle destination = graphics.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setQueueSharing(ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    );
    ASSERT_NE(destination.get(), nullptr);

    const GraphicsRuntime::TextureUploadRegion regions[] = {
        GraphicsRuntime::TextureUploadRegion{
            .data = mip0Bytes,
            .dataSize = sizeof(mip0Bytes),
            .rowPitch = 4u * 4u,
            .depthPitch = 4u * 4u * 4u,
            .arraySlice = 0u,
            .mipLevel = 0u,
        },
        GraphicsRuntime::TextureUploadRegion{
            .data = mip1Bytes,
            .dataSize = sizeof(mip1Bytes),
            .rowPitch = 2u * 4u,
            .depthPitch = 2u * 2u * 4u,
            .arraySlice = 0u,
            .mipLevel = 1u,
        },
    };
    QueueSubmissionToken acceptedToken;
    ASSERT_TRUE(graphics.uploadTextureBatch(GraphicsRuntime::TextureUploadBatchDesc{
        .destination = destination,
        .regions = regions,
        .regionCount = LengthOf(regions),
        .finalState = ResourceStates::ShaderResource,
        .physicalInitialState = ResourceStates::Unknown,
        .acceptedToken = &acceptedToken,
        .hasPhysicalInitialState = true,
    }));
    ASSERT_TRUE(acceptedToken.valid());

    // Graph declaration has already copied the regions into immutable blobs.  Overwrite the caller arrays before
    // the native work completes; readback must still observe the original bytes.
    NWB_MEMSET(mip0Bytes, 0, sizeof(mip0Bytes));
    NWB_MEMSET(mip1Bytes, 0, sizeof(mip1Bytes));
    ASSERT_TRUE(device.waitForIdle());

    const u8* const expectedMips[] = { expectedMip0, expectedMip1 };
    const u32 mipWidths[] = { 4u, 2u };
    const u32 mipHeights[] = { 4u, 2u };
    for(u32 mipLevel = 0u; mipLevel < LengthOf(expectedMips); ++mipLevel){
        StagingTextureHandle readback = device.createStagingTexture(destination->getDescription(), CpuAccessMode::Read);
        ASSERT_NE(readback.get(), nullptr);
        TextureSlice slice;
        slice.setMipLevel(mipLevel);
        CommandListHandle readbackCommandList = device.createCommandList();
        ASSERT_NE(readbackCommandList.get(), nullptr);
        readbackCommandList->open();
        ASSERT_TRUE(readbackCommandList->hasCommandBuffer());
        readbackCommandList->copyTexture(*readback, slice, *destination, slice);
        readbackCommandList->close();
        CommandList* const readbackLists[] = { readbackCommandList.get() };
        ASSERT_TRUE(device.executeCommandLists(
            readbackLists,
            LengthOf(readbackLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        ).valid());
        ASSERT_TRUE(device.waitForIdle());

        usize rowPitch = 0u;
        const u8* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
            *readback,
            slice,
            CpuAccessMode::Read,
            &rowPitch
        ));
        ASSERT_NE(readbackBytes, nullptr);
        ASSERT_GE(rowPitch, static_cast<usize>(mipWidths[mipLevel]) * 4u);
        for(u32 row = 0u; row < mipHeights[mipLevel]; ++row){
            for(usize byteIndex = 0u; byteIndex < static_cast<usize>(mipWidths[mipLevel]) * 4u; ++byteIndex){
                EXPECT_EQ(
                    readbackBytes[static_cast<usize>(row) * rowPitch + byteIndex],
                    expectedMips[mipLevel][static_cast<usize>(row) * mipWidths[mipLevel] * 4u + byteIndex]
                );
            }
        }
        device.unmapStagingTexture(*readback);
    }
}


TEST_F(DescriptorBufferRoundTripTest, StandaloneGraphTextureUploadDiscardsThenAcceptsTerminalCompletion){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    const TextureHandle destination = graphics.createTexture(
        TextureDesc()
            .setWidth(2u)
            .setHeight(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(destination.get(), nullptr);

    u8 uploadBytes[2u * 2u * 4u] = {};
    for(usize byteIndex = 0u; byteIndex < LengthOf(uploadBytes); ++byteIndex)
        uploadBytes[byteIndex] = static_cast<u8>(byteIndex * 19u + 5u);

    bool rejectedRecorded = false;
    bool rejectedAccepted = false;
    bool rejectedDiscarded = false;
    StandaloneGraphTextureUploadContext rejectedContext{
        .destination = destination,
        .bytes = uploadBytes,
        .byteCount = sizeof(uploadBytes),
        .rowPitch = 2u * 4u,
        .recorded = &rejectedRecorded,
        .accepted = &rejectedAccepted,
        .discarded = &rejectedDiscarded,
    };
    QueueSubmissionToken rejectedToken;
    {
        const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
        );
        ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        EXPECT_FALSE(graphics.submitStandaloneTaskGraph(
            &rejectedContext,
            &DeclareStandaloneGraphTextureUpload,
            rejectedToken,
            graphicsQueue
        ));
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(rejectedToken.valid());
    EXPECT_TRUE(rejectedRecorded);
    EXPECT_FALSE(rejectedAccepted);
    EXPECT_TRUE(rejectedDiscarded);

    bool acceptedRecorded = false;
    bool accepted = false;
    bool acceptedDiscarded = false;
    StandaloneGraphTextureUploadContext acceptedContext{
        .destination = destination,
        .bytes = uploadBytes,
        .byteCount = sizeof(uploadBytes),
        .rowPitch = 2u * 4u,
        .recorded = &acceptedRecorded,
        .accepted = &accepted,
        .discarded = &acceptedDiscarded,
    };
    QueueSubmissionToken acceptedToken;
    ASSERT_TRUE(graphics.submitStandaloneTaskGraph(
        &acceptedContext,
        &DeclareStandaloneGraphTextureUpload,
        acceptedToken,
        graphicsQueue
    ));
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.queue, CommandQueue::Graphics);
    EXPECT_TRUE(acceptedRecorded);
    EXPECT_TRUE(accepted);
    EXPECT_FALSE(acceptedDiscarded);
    EXPECT_TRUE(device.waitForIdle());
}


// The public standalone boundary uses Graphics' worker pool for normal packets. Two independent graph-owned blobs
// must record with distinct nonzero worker leases, submit in compiler order, and retain their copied bytes after
// the declaration intentionally clears both caller arrays.
TEST_F(DescriptorBufferRoundTripTest, StandaloneGraphReadyFrontierUploadsUseWorkerLeasesAndImmutableBlobs){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = BackendQueueId(device, CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    static constexpr u32 s_FirstExpectedWords[] = {
        0x4ef8a219u,
        0x13c0ffeeu,
        0x7f4a7c15u,
        0x9e3779b9u,
    };
    static constexpr u32 s_SecondExpectedWords[] = {
        0xfeedfaceu,
        0x0badf00du,
        0x2c1b3a49u,
        0xd1cebeefu,
    };
    u32 firstWords[] = {
        s_FirstExpectedWords[0u],
        s_FirstExpectedWords[1u],
        s_FirstExpectedWords[2u],
        s_FirstExpectedWords[3u],
    };
    u32 secondWords[] = {
        s_SecondExpectedWords[0u],
        s_SecondExpectedWords[1u],
        s_SecondExpectedWords[2u],
        s_SecondExpectedWords[3u],
    };
    const auto createDestination = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(sizeof(firstWords))
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
                .setCpuAccess(CpuAccessMode::Read)
        );
    };
    const BufferHandle firstDestination = createDestination();
    const BufferHandle secondDestination = createDestination();
    ASSERT_NE(firstDestination.get(), nullptr);
    ASSERT_NE(secondDestination.get(), nullptr);

    Latch recordingStarted(2);
    u32 firstWorkerIndex = 0u;
    u32 secondWorkerIndex = 0u;
    QueueSubmissionToken firstAcceptedToken;
    QueueSubmissionToken secondAcceptedToken;
    bool firstDiscarded = false;
    bool secondDiscarded = false;
    StandaloneGraphReadyFrontierUploadContext context{
        .firstDestination = firstDestination,
        .secondDestination = secondDestination,
        .firstBytes = firstWords,
        .secondBytes = secondWords,
        .firstByteCount = sizeof(firstWords),
        .secondByteCount = sizeof(secondWords),
        .recordingStarted = &recordingStarted,
        .firstWorkerIndex = &firstWorkerIndex,
        .secondWorkerIndex = &secondWorkerIndex,
        .firstAcceptedToken = &firstAcceptedToken,
        .secondAcceptedToken = &secondAcceptedToken,
        .firstDiscarded = &firstDiscarded,
        .secondDiscarded = &secondDiscarded,
    };
    QueueSubmissionToken terminalToken;
    const bool submitted = graphics.submitStandaloneTaskGraph(
        &context,
        &DeclareStandaloneGraphReadyFrontierUploads,
        terminalToken,
        graphicsQueue
    );
    ASSERT_TRUE(submitted) << "firstWorker=" << firstWorkerIndex
        << ", secondWorker=" << secondWorkerIndex
        << ", firstDiscarded=" << firstDiscarded
        << ", secondDiscarded=" << secondDiscarded;
    ASSERT_TRUE(terminalToken.valid());
    ASSERT_TRUE(firstAcceptedToken.valid());
    ASSERT_TRUE(secondAcceptedToken.valid());
    EXPECT_EQ(terminalToken.queue, CommandQueue::Graphics);
    EXPECT_GT(terminalToken.value, secondAcceptedToken.value);
    EXPECT_FALSE(firstDiscarded);
    EXPECT_FALSE(secondDiscarded);
    EXPECT_NE(firstWorkerIndex, 0u);
    EXPECT_NE(secondWorkerIndex, 0u);
    EXPECT_NE(firstWorkerIndex, secondWorkerIndex);
    for(usize wordIndex = 0u; wordIndex < LengthOf(firstWords); ++wordIndex){
        EXPECT_EQ(firstWords[wordIndex], 0u);
        EXPECT_EQ(secondWords[wordIndex], 0u);
    }

    ASSERT_TRUE(device.waitForIdle());
    const u32* const firstUploadedWords = static_cast<const u32*>(device.mapBuffer(*firstDestination, CpuAccessMode::Read));
    const u32* const secondUploadedWords = static_cast<const u32*>(device.mapBuffer(*secondDestination, CpuAccessMode::Read));
    ASSERT_NE(firstUploadedWords, nullptr);
    ASSERT_NE(secondUploadedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstExpectedWords); ++wordIndex){
        EXPECT_EQ(firstUploadedWords[wordIndex], s_FirstExpectedWords[wordIndex]);
        EXPECT_EQ(secondUploadedWords[wordIndex], s_SecondExpectedWords[wordIndex]);
    }
    device.unmapBuffer(*firstDestination);
    device.unmapBuffer(*secondDestination);
}


TEST_F(DescriptorBufferRoundTripTest, RetainedTextureTypedImportsTrackAcceptedMipStateCompleteness){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureHandle texture = graphics.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(texture.get(), nullptr);

    GpuTaskGraph freshImportGraph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId freshImport = freshImportGraph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/fresh_retained_unspecified_import"))
            .setMarkerLabel("Fresh Retained Unspecified Import")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(freshImport.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(freshImportGraph);

        EXPECT_EQ(declarations.resourceAt(freshImport.index).initialState, ResourceStates::ShaderResource);
    }

    u8 uploadBytes[4u * 4u * 4u] = {};
    const CommandListHandle upload = device.createCommandList();
    ASSERT_NE(upload.get(), nullptr);
    upload->open();
    ASSERT_TRUE(upload->tryWriteTexture(*texture, 0u, 0u, uploadBytes, 4u * 4u));
    upload->setTextureState(texture.get(), TextureSubresourceSet(0u, 1u, 0u, 1u), ResourceStates::ShaderResource);
    upload->close();

    // Closing only records the terminal restoration. A separate list must not inherit that speculative state
    // before Queue::submit accepts the producer.
    const CommandListHandle preSubmitStateProbe = device.createCommandList();
    ASSERT_NE(preSubmitStateProbe.get(), nullptr);
    preSubmitStateProbe->open();
    EXPECT_EQ(preSubmitStateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::Unknown);
    EXPECT_EQ(preSubmitStateProbe->getTextureSubresourceState(texture.get(), 0u, 1u), ResourceStates::Unknown);
    preSubmitStateProbe->close();

    CommandList* const commandLists[] = { upload.get() };
    const QueueSubmissionToken uploadToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(uploadToken.valid());

    // A successful submission publishes only the subresource whose recorded terminal barrier was accepted. The
    // untouched mip remains physically Undefined and therefore cannot inherit the retained descriptor state.
    const CommandListHandle postSubmitStateProbe = device.createCommandList();
    ASSERT_NE(postSubmitStateProbe.get(), nullptr);
    postSubmitStateProbe->open();
    EXPECT_EQ(postSubmitStateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(postSubmitStateProbe->getTextureSubresourceState(texture.get(), 0u, 1u), ResourceStates::Unknown);
    postSubmitStateProbe->close();

    GpuTaskGraph partialImportGraph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId partialImport = partialImportGraph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/partial_retained_unspecified_import"))
            .setMarkerLabel("Partial Retained Unspecified Import")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(partialImport.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(partialImportGraph);

        EXPECT_EQ(declarations.resourceAt(partialImport.index).initialState, ResourceStates::Unknown);
    }

    const CommandListHandle secondUpload = device.createCommandList();
    ASSERT_NE(secondUpload.get(), nullptr);
    secondUpload->open();
    ASSERT_TRUE(secondUpload->tryWriteTexture(*texture, 0u, 1u, uploadBytes, 2u * 4u));
    secondUpload->setTextureState(texture.get(), TextureSubresourceSet(1u, 1u, 0u, 1u), ResourceStates::ShaderResource);
    secondUpload->close();

    const CommandListHandle secondPreSubmitStateProbe = device.createCommandList();
    ASSERT_NE(secondPreSubmitStateProbe.get(), nullptr);
    secondPreSubmitStateProbe->open();
    EXPECT_EQ(secondPreSubmitStateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(secondPreSubmitStateProbe->getTextureSubresourceState(texture.get(), 0u, 1u), ResourceStates::Unknown);
    secondPreSubmitStateProbe->close();

    CommandList* const secondCommandLists[] = { secondUpload.get() };
    const QueueSubmissionToken secondUploadToken = device.executeCommandLists(
        secondCommandLists,
        LengthOf(secondCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(secondUploadToken.valid());
    EXPECT_EQ(secondUploadToken.queue, CommandQueue::Graphics);
    EXPECT_GT(secondUploadToken.value, uploadToken.value);

    const CommandListHandle completeStateProbe = device.createCommandList();
    ASSERT_NE(completeStateProbe.get(), nullptr);
    completeStateProbe->open();
    EXPECT_EQ(completeStateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(completeStateProbe->getTextureSubresourceState(texture.get(), 0u, 1u), ResourceStates::ShaderResource);
    completeStateProbe->close();

    GpuTaskGraph completeImportGraph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId completeImport = completeImportGraph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/complete_retained_unspecified_import"))
            .setMarkerLabel("Complete Retained Unspecified Import")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(completeImport.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(completeImportGraph);

        EXPECT_EQ(declarations.resourceAt(completeImport.index).initialState, ResourceStates::ShaderResource);
    }

    ASSERT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

