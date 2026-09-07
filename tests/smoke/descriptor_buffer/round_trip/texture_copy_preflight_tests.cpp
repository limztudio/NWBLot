// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, DirectTextureCopyRejectsAtomicallyAndRecoversWithReadback){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 s_Width = 8u;
    constexpr u32 s_Height = 8u;
    constexpr FormatSupport::Mask s_RequiredFormatSupport = FormatSupport::Texture
        | FormatSupport::RenderTarget
    ;
    if(
        (device.queryFormatSupport(Format::RGBA8_UNORM) & s_RequiredFormatSupport)
        != s_RequiredFormatSupport
    )
        GTEST_SKIP() << "Direct texture copy smoke: RGBA8 render targets are unavailable.";

    const TextureDesc textureDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const TextureHandle source = device.createTexture(textureDesc);
    const TextureHandle destination = device.createTexture(textureDesc);
    const TextureHandle priorWorkTarget = device.createTexture(textureDesc);
    const StagingTextureHandle readback = device.createStagingTexture(textureDesc, CpuAccessMode::Read);
    ASSERT_TRUE(source);
    ASSERT_TRUE(destination);
    ASSERT_TRUE(priorWorkTarget);
    ASSERT_TRUE(readback);

    const FramebufferHandle sourceFramebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(source.get())
    );
    const FramebufferHandle priorWorkFramebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(priorWorkTarget.get())
    );
    ASSERT_TRUE(sourceFramebuffer);
    ASSERT_TRUE(priorWorkFramebuffer);

    const Object sourceNativeImage = source->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Image);
    ASSERT_NE(sourceNativeImage.integer, 0u);
    constexpr VkImageUsageFlags s_SourceNativeUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ;
    const TextureHandle duplicateSourceWrapper = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        sourceNativeImage,
        textureDesc,
        GraphicsBackend::NativeTextureProvenance{ .usage = s_SourceNativeUsage }
    );
    EXPECT_FALSE(duplicateSourceWrapper);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(source.get()));

    TextureDesc invalidNativeDescs[3u] = { textureDesc, textureDesc, textureDesc };
    invalidNativeDescs[0u].sampleQuality = 1u;
    invalidNativeDescs[1u]
        .setDimension(TextureDimension::Texture1D)
        .setHeight(1u)
        .setDepth(1u)
        .setArraySize(1u)
        .setMipLevels(1u)
        .setSampleCount(2u)
    ;
    invalidNativeDescs[2u]
        .setDimension(TextureDimension::TextureCube)
        .setDepth(1u)
        .setArraySize(6u)
        .setMipLevels(1u)
        .setSampleCount(2u)
    ;
    constexpr const char* s_InvalidNativeLabels[] = {
        "nonzero sample quality",
        "multisampled 1D image",
        "multisampled cube image",
    };
    for(usize invalidIndex = 0u; invalidIndex < LengthOf(invalidNativeDescs); ++invalidIndex){
        SCOPED_TRACE(s_InvalidNativeLabels[invalidIndex]);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(device.createHandleForNativeTexture(
                GraphicsBackend::ObjectTypes::VK_Image,
                sourceNativeImage,
                invalidNativeDescs[invalidIndex],
                GraphicsBackend::NativeTextureProvenance{ .usage = s_SourceNativeUsage }
            ));
        }, "");
#else
        EXPECT_FALSE(device.createHandleForNativeTexture(
            GraphicsBackend::ObjectTypes::VK_Image,
            sourceNativeImage,
            invalidNativeDescs[invalidIndex],
            GraphicsBackend::NativeTextureProvenance{ .usage = s_SourceNativeUsage }
        ));
#endif
    }

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    const TextureHandle foreignSource = foreignDevice.createTexture(textureDesc);
    const TextureHandle foreignDestination = foreignDevice.createTexture(textureDesc);
    ASSERT_TRUE(foreignSource);
    ASSERT_TRUE(foreignDestination);

    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const auto submit = [&](){
        CommandList* const commandLists[] = { commandList.get() };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
    };
    commandList->open();
    commandList->close();
    QueueSubmissionToken lastAcceptedToken = submit();
    ASSERT_TRUE(lastAcceptedToken.valid());

    enum class Operation : u8{
        NullDestination,
        NullSource,
        SameResource,
        ForeignDestination,
        ForeignSource,
        DestinationSliceOutOfBounds,
        SourceSliceOutOfBounds,
        MismatchedExtent,
        MismatchedSampleCount,
        MismatchedFormat,
        MismatchedImageType,
        MismatchedAspectMetadata,
        MismatchedNativeExtentMetadata,
        NonzeroSampleQuality,
        SourcePermanentStateConflict,
        DestinationPermanentStateConflict,
    };
    struct Case{
        Operation operation;
        const char* label;
    };
    const Case cases[] = {
        { Operation::NullDestination, "null destination" },
        { Operation::NullSource, "null source" },
        { Operation::SameResource, "same resource" },
        { Operation::ForeignDestination, "foreign destination" },
        { Operation::ForeignSource, "foreign source" },
        { Operation::DestinationSliceOutOfBounds, "destination slice out of bounds" },
        { Operation::SourceSliceOutOfBounds, "source slice out of bounds" },
        { Operation::MismatchedExtent, "mismatched resolved extents" },
        { Operation::MismatchedSampleCount, "mismatched sample count" },
        { Operation::MismatchedFormat, "mismatched exact format" },
        { Operation::MismatchedImageType, "mismatched native image type" },
        { Operation::MismatchedAspectMetadata, "mismatched aspect metadata" },
        { Operation::MismatchedNativeExtentMetadata, "mismatched native extent metadata" },
        { Operation::NonzeroSampleQuality, "nonzero sample quality metadata" },
        { Operation::SourcePermanentStateConflict, "source permanent-state conflict" },
        { Operation::DestinationPermanentStateConflict, "destination permanent-state conflict" },
    };

    TextureDesc& mutableSourceDesc = const_cast<TextureDesc&>(source->getDescription());
    TextureDesc& mutableDestinationDesc = const_cast<TextureDesc&>(destination->getDescription());
    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        mutableSourceDesc = textureDesc;
        mutableDestinationDesc = textureDesc;
        Texture* caseDestination = destination.get();
        Texture* caseSource = source.get();
        TextureSlice destinationSlice;
        TextureSlice sourceSlice;
        bool sourcePermanentConflict = false;
        bool destinationPermanentConflict = false;
        switch(testCase.operation){
        case Operation::NullDestination:
            caseDestination = nullptr;
            break;
        case Operation::NullSource:
            caseSource = nullptr;
            break;
        case Operation::SameResource:
            caseDestination = source.get();
            break;
        case Operation::ForeignDestination:
            caseDestination = foreignDestination.get();
            break;
        case Operation::ForeignSource:
            caseSource = foreignSource.get();
            break;
        case Operation::DestinationSliceOutOfBounds:
            destinationSlice.setMipLevel(1u);
            break;
        case Operation::SourceSliceOutOfBounds:
            sourceSlice.setArraySlice(1u);
            break;
        case Operation::MismatchedExtent:
            destinationSlice.setSize(4u, 4u, 1u);
            break;
        case Operation::MismatchedSampleCount:
            mutableDestinationDesc.setSampleCount(2u);
            break;
        case Operation::MismatchedFormat:
            mutableDestinationDesc.setFormat(Format::RGBA8_UINT);
            break;
        case Operation::MismatchedImageType:
            mutableDestinationDesc.setDimension(TextureDimension::Texture1D).setHeight(1u);
            break;
        case Operation::MismatchedAspectMetadata:
            mutableSourceDesc.setFormat(Format::D32);
            mutableDestinationDesc.setFormat(Format::D32);
            break;
        case Operation::MismatchedNativeExtentMetadata:
            mutableSourceDesc.setWidth(s_Width - 1u);
            mutableDestinationDesc.setWidth(s_Width - 1u);
            break;
        case Operation::NonzeroSampleQuality:
            mutableSourceDesc.sampleQuality = 1u;
            mutableDestinationDesc.sampleQuality = 1u;
            break;
        case Operation::SourcePermanentStateConflict:
            sourcePermanentConflict = true;
            break;
        case Operation::DestinationPermanentStateConflict:
            destinationPermanentConflict = true;
            break;
        }

        commandList->open();
        if(sourcePermanentConflict)
            commandList->setPermanentTextureState(source.get(), ResourceStates::Common);
        if(destinationPermanentConflict)
            commandList->setPermanentTextureState(destination.get(), ResourceStates::Common);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->setGraphicsState(GraphicsState().setFramebuffer(priorWorkFramebuffer.get()));
        commandList->clearTextureFloat(
            priorWorkTarget.get(),
            s_AllSubresources,
            Color(0.25f, 0.5f, 0.75f, 1.f)
        );
        ASSERT_TRUE(commandList->isRenderPassActive());
        ASSERT_FALSE(commandList->commandRecordingFailed());

        const ResourceStates::Mask sourceStateBefore = commandList->getTextureSubresourceState(
            source.get(),
            0u,
            0u
        );
        const ResourceStates::Mask destinationStateBefore = commandList->getTextureSubresourceState(
            destination.get(),
            0u,
            0u
        );
        const usize sourceReferencesBefore = caseSource ? caseSource->getReferenceCount() : 0u;
        const usize destinationReferencesBefore = caseDestination ? caseDestination->getReferenceCount() : 0u;
        commandList->copyTexture(caseDestination, destinationSlice, caseSource, sourceSlice);
        mutableSourceDesc = textureDesc;
        mutableDestinationDesc = textureDesc;

        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->isRenderPassActive());
        EXPECT_EQ(
            commandList->getTextureSubresourceState(source.get(), 0u, 0u),
            sourceStateBefore
        );
        EXPECT_EQ(
            commandList->getTextureSubresourceState(destination.get(), 0u, 0u),
            destinationStateBefore
        );
        if(caseSource)
            EXPECT_EQ(caseSource->getReferenceCount(), sourceReferencesBefore);
        if(caseDestination)
            EXPECT_EQ(caseDestination->getReferenceCount(), destinationReferencesBefore);

        CommandListResourceStateHandoff invalidHandoff(DescriptorBufferRoundTripTest::arena());
        commandList->close(&invalidHandoff);
        EXPECT_FALSE(invalidHandoff.valid());
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());

        commandList->open();
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        const QueueSubmissionToken recoveredToken = submit();
        ASSERT_TRUE(recoveredToken.valid());
        EXPECT_EQ(recoveredToken.value, lastAcceptedToken.value + 1u);
        lastAcceptedToken = recoveredToken;
    }

    TextureSlice copiedSlice;
    copiedSlice.setOrigin(2u, 2u, 0u).setSize(4u, 4u, 1u);
    commandList->open();
    commandList->clearTextureFloat(
        destination.get(),
        s_AllSubresources,
        Color(1.f, 0.f, 0.f, 1.f)
    );
    commandList->setGraphicsState(GraphicsState().setFramebuffer(sourceFramebuffer.get()));
    commandList->clearTextureFloat(source.get(), s_AllSubresources, Color(0.f, 1.f, 0.f, 1.f));
    commandList->endRenderPass();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken seedToken = submit();
    ASSERT_TRUE(seedToken.valid());
    EXPECT_EQ(seedToken.value, lastAcceptedToken.value + 1u);
    ASSERT_TRUE(device.waitForIdle());
    lastAcceptedToken = seedToken;

    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(priorWorkFramebuffer.get()));
    commandList->clearTextureFloat(
        priorWorkTarget.get(),
        s_AllSubresources,
        Color(0.75f, 0.5f, 0.25f, 1.f)
    );
    ASSERT_TRUE(commandList->isRenderPassActive());
    const usize sourceReferencesBeforeCopy = source->getReferenceCount();
    const usize destinationReferencesBeforeCopy = destination->getReferenceCount();
    commandList->copyTexture(destination.get(), copiedSlice, source.get(), copiedSlice);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    EXPECT_EQ(commandList->getTextureSubresourceState(source.get(), 0u, 0u), ResourceStates::CopySource);
    EXPECT_EQ(
        commandList->getTextureSubresourceState(destination.get(), 0u, 0u),
        ResourceStates::CopyDest
    );
    EXPECT_EQ(source->getReferenceCount(), sourceReferencesBeforeCopy + 1u);
    EXPECT_EQ(destination->getReferenceCount(), destinationReferencesBeforeCopy + 1u);
    commandList->copyTexture(readback.get(), TextureSlice{}, destination.get(), TextureSlice{});
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken copyToken = submit();
    ASSERT_TRUE(copyToken.valid());
    EXPECT_EQ(copyToken.value, lastAcceptedToken.value + 1u);
    ASSERT_TRUE(device.waitForIdle());

    usize rowPitch = 0u;
    const u8* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        readback.get(),
        TextureSlice{},
        CpuAccessMode::Read,
        &rowPitch
    ));
    ASSERT_NE(readbackBytes, nullptr);
    ASSERT_GE(rowPitch, static_cast<usize>(s_Width) * 4u);
    for(u32 row = 0u; row < s_Height; ++row){
        for(u32 column = 0u; column < s_Width; ++column){
            const bool copied = column >= 2u && column < 6u && row >= 2u && row < 6u;
            const usize pixelOffset = static_cast<usize>(row) * rowPitch + static_cast<usize>(column) * 4u;
            EXPECT_EQ(readbackBytes[pixelOffset + 0u], copied ? 0u : 255u);
            EXPECT_EQ(readbackBytes[pixelOffset + 1u], copied ? 255u : 0u);
            EXPECT_EQ(readbackBytes[pixelOffset + 2u], 0u);
            EXPECT_EQ(readbackBytes[pixelOffset + 3u], 255u);
        }
    }
    device.unmapStagingTexture(readback.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

