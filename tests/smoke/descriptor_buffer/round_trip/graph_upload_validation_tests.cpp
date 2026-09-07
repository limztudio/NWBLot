// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Public texture uploads no longer retain a native fallback. A generic packed payload cannot describe the two
// independently copied aspects of a combined depth/stencil texture. Retaining an Unknown initial state also makes
// the post-upload state untrackable. Both cases must fail before a command list can claim a successful upload.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedTextureUploadsRejectUnsafeLegacyDescriptors){
    auto& graphics = s_scope->graphics();
    const u8 depthStencilBytes[8u] = {};
    const Format::Enum combinedDepthStencilFormats[] = {
        Format::D32S8,
    };
    for(const Format::Enum format : combinedDepthStencilFormats){
        Graphics::TextureSetupDesc setupDesc;
        setupDesc.textureDesc = TextureDesc()
            .setWidth(1u)
            .setHeight(1u)
            .setFormat(format)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::DepthWrite)
        ;
        setupDesc.data = depthStencilBytes;
        setupDesc.uploadDataSize = sizeof(depthStencilBytes);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            QueueSubmissionToken setupToken;
            setupDesc.acceptedToken = &setupToken;
            EXPECT_EQ(graphics.setupTexture(setupDesc).get(), nullptr);
        }, "");
#else
        QueueSubmissionToken setupToken;
        setupDesc.acceptedToken = &setupToken;
        const TextureHandle setupTexture = graphics.setupTexture(setupDesc);
        EXPECT_EQ(setupTexture.get(), nullptr);
        EXPECT_FALSE(setupToken.valid());
#endif
    }

    const TextureHandle depthStencilDestination = graphics.createTexture(
        TextureDesc()
            .setWidth(1u)
            .setHeight(1u)
            .setFormat(Format::D32S8)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::DepthWrite)
    );
    ASSERT_NE(depthStencilDestination.get(), nullptr);
    const Graphics::TextureUploadRegion depthStencilRegion{
        .data = depthStencilBytes,
        .dataSize = sizeof(depthStencilBytes),
        .rowPitch = 0u,
        .depthPitch = 0u,
        .arraySlice = 0u,
        .mipLevel = 0u,
    };
    const Graphics::TextureUploadBatchDesc depthStencilBatchDesc{
        .destination = depthStencilDestination,
        .regions = &depthStencilRegion,
        .regionCount = 1u,
        .finalState = ResourceStates::DepthWrite,
    };
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        QueueSubmissionToken depthStencilBatchToken;
        Graphics::TextureUploadBatchDesc rejectedBatchDesc = depthStencilBatchDesc;
        rejectedBatchDesc.acceptedToken = &depthStencilBatchToken;
        EXPECT_FALSE(graphics.uploadTextureBatch(rejectedBatchDesc));
    }, "");
#else
    QueueSubmissionToken depthStencilBatchToken;
    Graphics::TextureUploadBatchDesc depthStencilRejectedBatchDesc = depthStencilBatchDesc;
    depthStencilRejectedBatchDesc.acceptedToken = &depthStencilBatchToken;
    EXPECT_FALSE(graphics.uploadTextureBatch(depthStencilRejectedBatchDesc));
    EXPECT_FALSE(depthStencilBatchToken.valid());
#endif

    const u8 colorBytes[4u] = {};
    Graphics::TextureSetupDesc unknownSetupDesc;
    unknownSetupDesc.textureDesc = TextureDesc()
        .setWidth(1u)
        .setHeight(1u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Unknown)
        .setKeepInitialState(true)
    ;
    unknownSetupDesc.data = colorBytes;
    unknownSetupDesc.uploadDataSize = sizeof(colorBytes);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        QueueSubmissionToken unknownSetupToken;
        unknownSetupDesc.acceptedToken = &unknownSetupToken;
        EXPECT_EQ(graphics.setupTexture(unknownSetupDesc).get(), nullptr);
    }, "");
#else
    QueueSubmissionToken unknownSetupToken;
    unknownSetupDesc.acceptedToken = &unknownSetupToken;
    const TextureHandle unknownSetupTexture = graphics.setupTexture(unknownSetupDesc);
    EXPECT_EQ(unknownSetupTexture.get(), nullptr);
    EXPECT_FALSE(unknownSetupToken.valid());
#endif

    const TextureHandle unknownDestination = graphics.createTexture(unknownSetupDesc.textureDesc);
    ASSERT_NE(unknownDestination.get(), nullptr);
    const Graphics::TextureUploadRegion unknownRegion{
        .data = colorBytes,
        .dataSize = sizeof(colorBytes),
        .rowPitch = 0u,
        .depthPitch = 0u,
        .arraySlice = 0u,
        .mipLevel = 0u,
    };
    const Graphics::TextureUploadBatchDesc unknownBatchDesc{
        .destination = unknownDestination,
        .regions = &unknownRegion,
        .regionCount = 1u,
        .finalState = ResourceStates::CopyDest,
    };
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        QueueSubmissionToken unknownBatchToken;
        Graphics::TextureUploadBatchDesc rejectedBatchDesc = unknownBatchDesc;
        rejectedBatchDesc.acceptedToken = &unknownBatchToken;
        EXPECT_FALSE(graphics.uploadTextureBatch(rejectedBatchDesc));
    }, "");
#else
    QueueSubmissionToken unknownBatchToken;
    Graphics::TextureUploadBatchDesc unknownRejectedBatchDesc = unknownBatchDesc;
    unknownRejectedBatchDesc.acceptedToken = &unknownBatchToken;
    EXPECT_FALSE(graphics.uploadTextureBatch(unknownRejectedBatchDesc));
    EXPECT_FALSE(unknownBatchToken.valid());
#endif
}


// Combined depth/stencil uploads are representable once each immutable payload names its exact aspect. The graph
// records the two copies serially, publishes their shared subresource as DepthWrite, and keeps them on Graphics so
// this backend never relies on optional depth/stencil copy-on-Compute/Transfer format features.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedTextureUploadsAcceptExplicitDepthStencilAspectPlanes){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();

    const f32 setupDepthPlane = 0.375f;
    QueueSubmissionToken setupToken;
    Graphics::TextureSetupDesc setupDesc;
    setupDesc.textureDesc = TextureDesc()
        .setWidth(1u)
        .setHeight(1u)
        .setFormat(Format::D32S8)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::DepthWrite)
    ;
    setupDesc.data = &setupDepthPlane;
    setupDesc.uploadDataSize = sizeof(setupDepthPlane);
    // An explicit non-Graphics preference must resolve safely rather than emit a depth copy on a queue family
    // whose per-format depth-copy capability the backend does not negotiate.
    setupDesc.queue = CommandQueue::Transfer;
    setupDesc.acceptedToken = &setupToken;
    setupDesc.aspect = TextureUploadAspect::Depth;
    const TextureHandle destination = graphics.setupTexture(setupDesc);
    ASSERT_NE(destination.get(), nullptr);
    ASSERT_TRUE(setupToken.valid());
    EXPECT_EQ(setupToken.queue, CommandQueue::Graphics);

    const f32 batchDepthPlane = 0.625f;
    const u8 batchStencilPlane = 0x5du;
    const Graphics::TextureUploadRegion regions[] = {
        Graphics::TextureUploadRegion{
            .data = &batchDepthPlane,
            .dataSize = sizeof(batchDepthPlane),
            .arraySlice = 0u,
            .mipLevel = 0u,
            .aspect = TextureUploadAspect::Depth,
        },
        Graphics::TextureUploadRegion{
            .data = &batchStencilPlane,
            .dataSize = sizeof(batchStencilPlane),
            .arraySlice = 0u,
            .mipLevel = 0u,
            .aspect = TextureUploadAspect::Stencil,
        },
    };
    QueueSubmissionToken batchToken;
    ASSERT_TRUE(graphics.uploadTextureBatch(Graphics::TextureUploadBatchDesc{
        .destination = destination,
        .regions = regions,
        .regionCount = LengthOf(regions),
        .finalState = ResourceStates::DepthWrite,
        .queue = CommandQueue::Transfer,
        .acceptedToken = &batchToken,
    }));
    ASSERT_TRUE(batchToken.valid());
    EXPECT_EQ(batchToken.queue, CommandQueue::Graphics);
    EXPECT_TRUE(device.waitForIdle());
}


// Public buffer uploads must not claim success through the former direct route when VkBufferCopy would reject the
// region, nor can a retained Unknown state provide the graph-visible final state expected by the next consumer.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedBufferUploadsRejectUnsafeLegacyDescriptors){
    auto& graphics = s_scope->graphics();
    const u8 oneByte = 0xabu;
    const u8 alignedBytes[4u] = {};

    Graphics::BufferSetupDesc unalignedSizeDesc;
    unalignedSizeDesc.bufferDesc = BufferDesc()
        .setByteSize(sizeof(alignedBytes))
        .setInitialState(ResourceStates::Common)
    ;
    unalignedSizeDesc.data = &oneByte;
    unalignedSizeDesc.dataSize = sizeof(oneByte);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        QueueSubmissionToken unalignedSizeToken;
        unalignedSizeDesc.acceptedToken = &unalignedSizeToken;
        EXPECT_EQ(graphics.setupBuffer(unalignedSizeDesc).get(), nullptr);
    }, "");
#else
    QueueSubmissionToken unalignedSizeToken;
    unalignedSizeDesc.acceptedToken = &unalignedSizeToken;
    EXPECT_EQ(graphics.setupBuffer(unalignedSizeDesc).get(), nullptr);
    EXPECT_FALSE(unalignedSizeToken.valid());
#endif

    Graphics::BufferSetupDesc unalignedOffsetDesc;
    unalignedOffsetDesc.bufferDesc = BufferDesc()
        .setByteSize(8u)
        .setInitialState(ResourceStates::Common)
    ;
    unalignedOffsetDesc.data = alignedBytes;
    unalignedOffsetDesc.dataSize = sizeof(alignedBytes);
    unalignedOffsetDesc.destOffsetBytes = 2u;
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        QueueSubmissionToken unalignedOffsetToken;
        unalignedOffsetDesc.acceptedToken = &unalignedOffsetToken;
        EXPECT_EQ(graphics.setupBuffer(unalignedOffsetDesc).get(), nullptr);
    }, "");
#else
    QueueSubmissionToken unalignedOffsetToken;
    unalignedOffsetDesc.acceptedToken = &unalignedOffsetToken;
    EXPECT_EQ(graphics.setupBuffer(unalignedOffsetDesc).get(), nullptr);
    EXPECT_FALSE(unalignedOffsetToken.valid());
#endif

    Graphics::BufferSetupDesc unknownSetupDesc;
    unknownSetupDesc.bufferDesc = BufferDesc()
        .setByteSize(sizeof(alignedBytes))
        .setInitialState(ResourceStates::Unknown)
        .setKeepInitialState(true)
    ;
    unknownSetupDesc.data = alignedBytes;
    unknownSetupDesc.dataSize = sizeof(alignedBytes);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        QueueSubmissionToken unknownSetupToken;
        unknownSetupDesc.acceptedToken = &unknownSetupToken;
        EXPECT_EQ(graphics.setupBuffer(unknownSetupDesc).get(), nullptr);
    }, "");
#else
    QueueSubmissionToken unknownSetupToken;
    unknownSetupDesc.acceptedToken = &unknownSetupToken;
    EXPECT_EQ(graphics.setupBuffer(unknownSetupDesc).get(), nullptr);
    EXPECT_FALSE(unknownSetupToken.valid());
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

