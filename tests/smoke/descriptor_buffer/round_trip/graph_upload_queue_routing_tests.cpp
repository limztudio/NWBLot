// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A transfer-only family is optional, but when available it must be a real physical transport: Graphics uploads a
// shared buffer, Transfer copies it, and Graphics imports the result again through timeline waits. The buffers use
// concurrent Graphics/Transfer sharing, so this validates the new family set without manufacturing ownership aliases.
TEST_F(DescriptorBufferRoundTripTest, DedicatedTransferQueueCopiesConcurrentBufferRoundTrip){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Transfer queue: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& device = transferScope.graphics().getDevice();
    if(!device.getQueue(CommandQueue::Transfer))
        GTEST_SKIP() << "Transfer queue: adapter has no dedicated transfer-only queue family.";

    EXPECT_TRUE(device.usesConcurrentQueueSharing(ResourceQueueSharing::GraphicsAndTransfer));

    static constexpr u32 s_CopyWords[] = {
        0x0347a2d1u,
        0x89abcdefu,
        0x5162f093u,
        0xc0ffee42u,
    };
    const BufferDesc sourceDesc = BufferDesc()
        .setByteSize(sizeof(s_CopyWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(sizeof(s_CopyWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto source = device.createBuffer(sourceDesc);
    auto destination = device.createBuffer(destinationDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);

    CommandListResourceStateHandoff graphicsToTransfer(transferScope.arena());
    auto graphicsProducer = device.createCommandList();
    ASSERT_NE(graphicsProducer.get(), nullptr);
    graphicsProducer->open();
    graphicsProducer->writeBuffer(source.get(), s_CopyWords, sizeof(s_CopyWords));
    graphicsProducer->setBufferState(source.get(), ResourceStates::CopySource);
    graphicsProducer->close(&graphicsToTransfer);
    ASSERT_TRUE(graphicsToTransfer.valid());

    CommandList* graphicsProducerLists[] = { graphicsProducer.get() };
    const QueueSubmissionToken graphicsProducerToken = device.executeCommandLists(
        graphicsProducerLists,
        LengthOf(graphicsProducerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsProducerToken.valid());
    ASSERT_EQ(graphicsProducerToken.queue, CommandQueue::Graphics);

    CommandListParameters transferParams;
    transferParams.setQueueType(CommandQueue::Transfer);
    CommandListResourceStateHandoff transferToGraphics(transferScope.arena());
    auto transferCopy = device.createCommandList(transferParams);
    ASSERT_NE(transferCopy.get(), nullptr);
    transferCopy->open(&graphicsToTransfer);
    EXPECT_EQ(transferCopy->getBufferState(source.get()), ResourceStates::CopySource);
    transferCopy->copyBuffer(destination.get(), 0u, source.get(), 0u, sizeof(s_CopyWords));
    transferCopy->close(&transferToGraphics);
    ASSERT_TRUE(transferToGraphics.valid());

    const QueueSubmissionToken transferWaits[] = { graphicsProducerToken };
    CommandList* transferCopyLists[] = { transferCopy.get() };
    const QueueSubmissionToken transferToken = device.executeCommandLists(
        transferCopyLists,
        LengthOf(transferCopyLists),
        CommandQueue::Transfer,
        QueueSubmissionDesc().setWaitTokens(transferWaits, LengthOf(transferWaits))
    );
    ASSERT_TRUE(transferToken.valid());
    ASSERT_EQ(transferToken.queue, CommandQueue::Transfer);

    auto graphicsConsumer = device.createCommandList();
    ASSERT_NE(graphicsConsumer.get(), nullptr);
    graphicsConsumer->open(&transferToGraphics);
    EXPECT_EQ(graphicsConsumer->getBufferState(destination.get()), ResourceStates::CopyDest);
    graphicsConsumer->setBufferState(destination.get(), ResourceStates::ShaderResource);
    graphicsConsumer->close();

    const QueueSubmissionToken graphicsConsumerWaits[] = { transferToken };
    CommandList* graphicsConsumerLists[] = { graphicsConsumer.get() };
    const QueueSubmissionToken graphicsConsumerToken = device.executeCommandLists(
        graphicsConsumerLists,
        LengthOf(graphicsConsumerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc().setWaitTokens(graphicsConsumerWaits, LengthOf(graphicsConsumerWaits))
    );
    ASSERT_TRUE(graphicsConsumerToken.valid());
    ASSERT_EQ(graphicsConsumerToken.queue, CommandQueue::Graphics);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_CopyWords); ++wordIndex)
        EXPECT_EQ(copiedWords[wordIndex], s_CopyWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// Graph-owned setup uploads expose only a returned resource handle, so an automatic Transfer producer must publish
// readiness onto Graphics before it returns. The graph-owned Graphics bridge carries the producer wait, so this
// deliberately submits the following Graphics copy without an explicit wait token; queue order keeps legacy callers
// and graph imports safe.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSetupBufferUsesDedicatedTransferAndBridgesGraphicsReadiness){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Setup upload Transfer route: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& graphics = transferScope.graphics();
    auto& device = graphics.getDevice();
    if(!device.getQueue(CommandQueue::Transfer))
        GTEST_SKIP() << "Setup upload Transfer route: adapter has no dedicated transfer-only queue family.";

    static constexpr usize s_UploadByteSize = 1024u * 1024u;
    static constexpr usize s_UploadWordCount = s_UploadByteSize / sizeof(u32);
    Vector<u32, Alloc::GlobalArena> uploadWords(transferScope.arena());
    uploadWords.resize(s_UploadWordCount);
    for(usize wordIndex = 0u; wordIndex < s_UploadWordCount; ++wordIndex)
        uploadWords[wordIndex] = 0x9e3779b9u * static_cast<u32>(wordIndex) + 0x5a17c3e1u;

    QueueSubmissionToken uploadToken;
    GraphicsRuntime::BufferSetupDesc setupDesc;
    setupDesc.bufferDesc = BufferDesc()
        .setByteSize(s_UploadByteSize)
        .setInitialState(ResourceStates::Common)
    ;
    setupDesc.data = uploadWords.data();
    setupDesc.dataSize = s_UploadByteSize;
    setupDesc.acceptedToken = &uploadToken;
    const BufferHandle source = graphics.setupBuffer(setupDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_TRUE(uploadToken.valid());
    ASSERT_EQ(uploadToken.queue, CommandQueue::Transfer);
    EXPECT_EQ(source->getDescription().queueSharing, ResourceQueueSharing::GraphicsAndTransfer);

    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(s_UploadByteSize)
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    const BufferHandle destination = device.createBuffer(destinationDesc);
    ASSERT_NE(destination.get(), nullptr);

    auto graphicsCopy = device.createCommandList();
    ASSERT_NE(graphicsCopy.get(), nullptr);
    graphicsCopy->open();
    graphicsCopy->copyBuffer(destination.get(), 0u, source.get(), 0u, s_UploadByteSize);
    graphicsCopy->close();

    CommandList* graphicsCopyLists[] = { graphicsCopy.get() };
    const QueueSubmissionToken graphicsCopyToken = device.executeCommandLists(
        graphicsCopyLists,
        LengthOf(graphicsCopyLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsCopyToken.valid());
    ASSERT_EQ(graphicsCopyToken.queue, CommandQueue::Graphics);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    const usize sampledWords[] = { 0u, s_UploadWordCount / 2u, s_UploadWordCount - 1u };
    for(const usize wordIndex : sampledWords)
        EXPECT_EQ(copiedWords[wordIndex], uploadWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// A standalone setup graph has no pre-existing packet load, so a large explicitly Graphics-routed upload may offload
// to an auxiliary Graphics transport only when it also inserts the primary-Graphics readiness packet before
// returning the public handle. The following direct primary copy deliberately carries no explicit wait token.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSetupBufferUsesAuxiliaryGraphicsAndBridgesPrimaryReadiness){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setTransferQueueEnabled(false));
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    ASSERT_TRUE(multiQueueScope.setCrossFamilySameClassQueueRoutingEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Same-class setup upload routing: no usable headless Vulkan device on this host.";

    auto& graphics = multiQueueScope.graphics();
    auto& device = graphics.getDevice();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const primaryGraphicsInfo = device.getPhysicalQueueInfo(primaryGraphicsQueue);
    ASSERT_NE(primaryGraphicsInfo, nullptr);
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* auxiliaryGraphicsInfo = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass != CommandQueue::Graphics
            || candidate.id == primaryGraphicsQueue
            || (auxiliaryGraphicsInfo && candidate.id.index >= auxiliaryGraphicsInfo->id.index)
        )
            continue;
        auxiliaryGraphicsInfo = &candidate;
    }
    if(!auxiliaryGraphicsInfo)
        GTEST_SKIP() << "Same-class setup upload routing: adapter exposes only one Graphics transport.";

    static constexpr usize s_UploadByteSize = 1024u * 1024u;
    static constexpr usize s_UploadWordCount = s_UploadByteSize / sizeof(u32);
    Vector<u32, Alloc::GlobalArena> uploadWords(multiQueueScope.arena());
    uploadWords.resize(s_UploadWordCount);
    for(usize wordIndex = 0u; wordIndex < s_UploadWordCount; ++wordIndex)
        uploadWords[wordIndex] = 0x51ed270bu * static_cast<u32>(wordIndex) + 0x8d1c79f3u;

    QueueSubmissionToken uploadToken;
    GraphicsRuntime::BufferSetupDesc setupDesc;
    setupDesc.bufferDesc = BufferDesc()
        .setByteSize(s_UploadByteSize)
        .setInitialState(ResourceStates::Common)
    ;
    setupDesc.data = uploadWords.data();
    setupDesc.dataSize = s_UploadByteSize;
    setupDesc.queue = CommandQueue::Graphics;
    setupDesc.acceptedToken = &uploadToken;
    const BufferHandle source = graphics.setupBuffer(setupDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_TRUE(uploadToken.valid());
    ASSERT_TRUE(uploadToken.matchesPhysicalQueue(
        auxiliaryGraphicsInfo->id.index,
        auxiliaryGraphicsInfo->id.deviceGeneration
    ));
    EXPECT_EQ(uploadToken.queue, CommandQueue::Graphics);
    EXPECT_EQ(
        source->getDescription().queueSharing,
        auxiliaryGraphicsInfo->familyIndex != primaryGraphicsInfo->familyIndex
            ? ResourceQueueSharing::Graphics
            : ResourceQueueSharing::Exclusive
    );

    const BufferHandle destination = device.createBuffer(
        BufferDesc()
            .setByteSize(s_UploadByteSize)
            .setInitialState(ResourceStates::Common)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(destination.get(), nullptr);

    auto primaryCopy = device.createCommandList();
    ASSERT_NE(primaryCopy.get(), nullptr);
    primaryCopy->open();
    primaryCopy->copyBuffer(destination.get(), 0u, source.get(), 0u, s_UploadByteSize);
    primaryCopy->close();

    CommandList* primaryCopyLists[] = { primaryCopy.get() };
    const QueueSubmissionToken primaryCopyToken = device.executeCommandLists(
        primaryCopyLists,
        LengthOf(primaryCopyLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(primaryCopyToken.valid());
    ASSERT_TRUE(primaryCopyToken.matchesPhysicalQueue(
        primaryGraphicsQueue.index,
        primaryGraphicsQueue.deviceGeneration
    ));
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    const usize sampledWords[] = { 0u, s_UploadWordCount / 2u, s_UploadWordCount - 1u };
    for(const usize wordIndex : sampledWords)
        EXPECT_EQ(copiedWords[wordIndex], uploadWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// Multi-subresource assets are a serial chain, not independent work. Once its first large region offloads to an
// auxiliary Graphics queue, every later mip stays there; the terminal graph-owned primary bridge still makes an
// immediate direct Graphics readback safe without exposing a completion token to the caller.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedTextureBatchUsesAuxiliaryGraphicsAndBridgesPrimaryReadiness){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setTransferQueueEnabled(false));
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    ASSERT_TRUE(multiQueueScope.setCrossFamilySameClassQueueRoutingEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Same-class texture batch routing: no usable headless Vulkan device on this host.";

    auto& graphics = multiQueueScope.graphics();
    auto& device = graphics.getDevice();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const primaryGraphicsInfo = device.getPhysicalQueueInfo(primaryGraphicsQueue);
    ASSERT_NE(primaryGraphicsInfo, nullptr);
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* auxiliaryGraphicsInfo = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass != CommandQueue::Graphics
            || candidate.id == primaryGraphicsQueue
            || (auxiliaryGraphicsInfo && candidate.id.index >= auxiliaryGraphicsInfo->id.index)
        )
            continue;
        auxiliaryGraphicsInfo = &candidate;
    }
    if(!auxiliaryGraphicsInfo)
        GTEST_SKIP() << "Same-class texture batch routing: adapter exposes only one Graphics transport.";

    static constexpr u32 s_Mip0Width = 512u;
    static constexpr u32 s_Mip0Height = 512u;
    static constexpr u32 s_Mip1Width = s_Mip0Width / 2u;
    static constexpr u32 s_Mip1Height = s_Mip0Height / 2u;
    static constexpr usize s_Mip0ByteSize = static_cast<usize>(s_Mip0Width) * s_Mip0Height * 4u;
    static constexpr usize s_Mip1ByteSize = static_cast<usize>(s_Mip1Width) * s_Mip1Height * 4u;
    Vector<u8, Alloc::GlobalArena> mip0Bytes(multiQueueScope.arena());
    Vector<u8, Alloc::GlobalArena> mip1Bytes(multiQueueScope.arena());
    mip0Bytes.resize(s_Mip0ByteSize);
    mip1Bytes.resize(s_Mip1ByteSize);
    for(usize byteIndex = 0u; byteIndex < mip0Bytes.size(); ++byteIndex)
        mip0Bytes[byteIndex] = static_cast<u8>(byteIndex * 13u + 0x27u);
    for(usize byteIndex = 0u; byteIndex < mip1Bytes.size(); ++byteIndex)
        mip1Bytes[byteIndex] = static_cast<u8>(byteIndex * 29u + 0x91u);

    const TextureHandle destination = graphics.createTexture(
        TextureDesc()
            .setWidth(s_Mip0Width)
            .setHeight(s_Mip0Height)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setQueueSharing(ResourceQueueSharing::Graphics)
    );
    ASSERT_NE(destination.get(), nullptr);
    Texture* const initialTextures[] = { destination.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::ShaderResource
    ));
    const GraphicsRuntime::TextureUploadRegion regions[] = {
        GraphicsRuntime::TextureUploadRegion{
            .data = mip0Bytes.data(),
            .dataSize = mip0Bytes.size(),
            .rowPitch = s_Mip0Width * 4u,
            .depthPitch = s_Mip0Width * s_Mip0Height * 4u,
            .arraySlice = 0u,
            .mipLevel = 0u,
        },
        GraphicsRuntime::TextureUploadRegion{
            .data = mip1Bytes.data(),
            .dataSize = mip1Bytes.size(),
            .rowPitch = s_Mip1Width * 4u,
            .depthPitch = s_Mip1Width * s_Mip1Height * 4u,
            .arraySlice = 0u,
            .mipLevel = 1u,
        },
    };
    QueueSubmissionToken uploadToken;
    ASSERT_TRUE(graphics.uploadTextureBatch(GraphicsRuntime::TextureUploadBatchDesc{
        .destination = destination,
        .regions = regions,
        .regionCount = LengthOf(regions),
        .finalState = ResourceStates::ShaderResource,
        .queue = CommandQueue::Graphics,
        .acceptedToken = &uploadToken,
        .physicalInitialState = ResourceStates::ShaderResource,
        .hasPhysicalInitialState = true,
    }));
    ASSERT_TRUE(uploadToken.valid());
    ASSERT_TRUE(uploadToken.matchesPhysicalQueue(
        auxiliaryGraphicsInfo->id.index,
        auxiliaryGraphicsInfo->id.deviceGeneration
    ));
    EXPECT_EQ(destination->getDescription().queueSharing, ResourceQueueSharing::Graphics);

    const u8* const expectedMips[] = { mip0Bytes.data(), mip1Bytes.data() };
    const u32 mipWidths[] = { s_Mip0Width, s_Mip1Width };
    const u32 mipHeights[] = { s_Mip0Height, s_Mip1Height };
    for(u32 mipLevel = 0u; mipLevel < LengthOf(expectedMips); ++mipLevel){
        StagingTextureHandle readback = device.createStagingTexture(destination->getDescription(), CpuAccessMode::Read);
        ASSERT_NE(readback.get(), nullptr);
        TextureSlice slice;
        slice.setMipLevel(mipLevel);
        CommandListHandle readbackCommandList = device.createCommandList();
        ASSERT_NE(readbackCommandList.get(), nullptr);
        readbackCommandList->open();
        readbackCommandList->copyTexture(readback.get(), slice, destination.get(), slice);
        readbackCommandList->close();
        CommandList* const readbackLists[] = { readbackCommandList.get() };
        const QueueSubmissionToken readbackToken = device.executeCommandLists(
            readbackLists,
            LengthOf(readbackLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(readbackToken.valid());
        ASSERT_TRUE(readbackToken.matchesPhysicalQueue(
            primaryGraphicsQueue.index,
            primaryGraphicsQueue.deviceGeneration
        ));
        ASSERT_TRUE(device.waitForIdle());

        usize rowPitch = 0u;
        const u8* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
            readback.get(),
            slice,
            CpuAccessMode::Read,
            &rowPitch
        ));
        ASSERT_NE(readbackBytes, nullptr);
        const u32 sampleXs[] = { 0u, mipWidths[mipLevel] / 2u, mipWidths[mipLevel] - 1u };
        const u32 sampleYs[] = { 0u, mipHeights[mipLevel] / 2u, mipHeights[mipLevel] - 1u };
        for(usize sampleIndex = 0u; sampleIndex < LengthOf(sampleXs); ++sampleIndex){
            const usize expectedOffset = (
                static_cast<usize>(sampleYs[sampleIndex]) * mipWidths[mipLevel] + sampleXs[sampleIndex]
            ) * 4u;
            const usize readbackOffset = static_cast<usize>(sampleYs[sampleIndex]) * rowPitch + sampleXs[sampleIndex] * 4u;
            for(usize component = 0u; component < 4u; ++component)
                EXPECT_EQ(readbackBytes[readbackOffset + component], expectedMips[mipLevel][expectedOffset + component]);
        }
        device.unmapStagingTexture(readback.get());
    }
}


// If no dedicated Transfer family exists, automatic sizeable setup uploads must still choose a real transport: a
// dedicated Compute queue when one is available, otherwise the established Graphics path. The accepted token makes
// this routing observable without exposing a backend-specific queue object through the public setup API.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSetupBufferAutomaticallyFallsBackWithoutTransfer){
    HeadlessGraphicsScope fallbackScope;
    ASSERT_TRUE(fallbackScope.setTransferQueueEnabled(false));
    if(!fallbackScope.initialize())
        GTEST_SKIP() << "Setup upload fallback: no usable headless Vulkan device on this host.";

    auto& graphics = fallbackScope.graphics();
    auto& device = graphics.getDevice();
    ASSERT_EQ(device.getQueue(CommandQueue::Transfer), nullptr);

    static constexpr usize s_UploadByteSize = 1024u * 1024u;
    Vector<u8, Alloc::GlobalArena> uploadBytes(fallbackScope.arena());
    uploadBytes.resize(s_UploadByteSize);
    for(usize byteIndex = 0u; byteIndex < s_UploadByteSize; ++byteIndex)
        uploadBytes[byteIndex] = static_cast<u8>(byteIndex);

    QueueSubmissionToken uploadToken;
    GraphicsRuntime::BufferSetupDesc setupDesc;
    setupDesc.bufferDesc = BufferDesc()
        .setByteSize(s_UploadByteSize)
        .setInitialState(ResourceStates::Common)
    ;
    setupDesc.data = uploadBytes.data();
    setupDesc.dataSize = s_UploadByteSize;
    setupDesc.acceptedToken = &uploadToken;
    const BufferHandle uploaded = graphics.setupBuffer(setupDesc);
    ASSERT_NE(uploaded.get(), nullptr);
    ASSERT_TRUE(uploadToken.valid());

    const CommandQueue::Enum expectedQueue = device.getQueue(CommandQueue::Compute)
        ? CommandQueue::Compute
        : CommandQueue::Graphics
    ;
    EXPECT_EQ(uploadToken.queue, expectedQueue);
    EXPECT_EQ(
        uploaded->getDescription().queueSharing,
        expectedQueue == CommandQueue::Compute
            ? ResourceQueueSharing::GraphicsAndAsyncCompute
            : ResourceQueueSharing::Exclusive
    );
    EXPECT_TRUE(device.waitForIdle());
}


// Exercise the texture path through the same automatic resolver. A one-mip 512x512 RGBA texture is deliberately
// large enough to cross the automatic-transfer threshold; with Transfer disabled it must use Compute when present
// or retain Graphics, while publishing the requested ShaderResource state before the setup call returns.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSetupTextureAutomaticallyFallsBackWithoutTransfer){
    HeadlessGraphicsScope fallbackScope;
    ASSERT_TRUE(fallbackScope.setTransferQueueEnabled(false));
    if(!fallbackScope.initialize())
        GTEST_SKIP() << "Setup texture fallback: no usable headless Vulkan device on this host.";

    auto& graphics = fallbackScope.graphics();
    auto& device = graphics.getDevice();
    ASSERT_EQ(device.getQueue(CommandQueue::Transfer), nullptr);

    static constexpr u32 s_TextureWidth = 512u;
    static constexpr u32 s_TextureHeight = 512u;
    static constexpr usize s_UploadByteSize = static_cast<usize>(s_TextureWidth) * s_TextureHeight * 4u;
    Vector<u8, Alloc::GlobalArena> uploadBytes(fallbackScope.arena());
    uploadBytes.resize(s_UploadByteSize);
    for(usize byteIndex = 0u; byteIndex < s_UploadByteSize; ++byteIndex)
        uploadBytes[byteIndex] = static_cast<u8>(byteIndex * 17u);

    QueueSubmissionToken uploadToken;
    GraphicsRuntime::TextureSetupDesc setupDesc;
    setupDesc.textureDesc = TextureDesc()
        .setWidth(s_TextureWidth)
        .setHeight(s_TextureHeight)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::ShaderResource)
    ;
    setupDesc.data = uploadBytes.data();
    setupDesc.uploadDataSize = s_UploadByteSize;
    setupDesc.acceptedToken = &uploadToken;
    const TextureHandle uploaded = graphics.setupTexture(setupDesc);
    ASSERT_NE(uploaded.get(), nullptr);
    ASSERT_TRUE(uploadToken.valid());

    const CommandQueue::Enum expectedQueue = device.getQueue(CommandQueue::Compute)
        ? CommandQueue::Compute
        : CommandQueue::Graphics
    ;
    EXPECT_EQ(uploadToken.queue, expectedQueue);
    EXPECT_EQ(
        uploaded->getDescription().queueSharing,
        expectedQueue == CommandQueue::Compute
            ? ResourceQueueSharing::GraphicsAndAsyncCompute
            : ResourceQueueSharing::Exclusive
    );
    EXPECT_EQ(uploaded->getDescription().initialState, ResourceStates::ShaderResource);
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

