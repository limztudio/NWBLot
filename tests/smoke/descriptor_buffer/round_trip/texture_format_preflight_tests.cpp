// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Vulkan forbids vkCmdClearColorImage for block-compressed images. A full BC1 clear must therefore use the
// staging-copy implementation for every requested mip/layer, retain its image until completion, and leave CopyDest
// as the direct command-list state. Unsupported encoders and impossible multisample metadata must reject atomically.
TEST_F(DescriptorBufferRoundTripTest, BlockCompressedFullClearUsesStagingAndRejectsUnsupportedPatterns){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 s_Width = 8u;
    constexpr u32 s_Height = 8u;
    constexpr u32 s_MipCount = 2u;
    constexpr u32 s_ArraySize = 2u;
    constexpr u32 s_BC1BlockWidth = 4u;
    constexpr u32 s_BC1BlockHeight = 4u;
    constexpr u32 s_BC1BlockByteSize = 8u;
    constexpr u8 s_ExpectedBC1Block[s_BC1BlockByteSize] = {
        0x1fu, 0xf8u, 0x1fu, 0xf8u, 0u, 0u, 0u, 0u,
    };
    if((device.queryFormatSupport(Format::BC1_UNORM) & FormatSupport::Texture) != FormatSupport::Texture)
        GTEST_SKIP() << "Block-compressed clear smoke: BC1_UNORM sampled images are unavailable.";

    const TextureDesc textureDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setMipLevels(s_MipCount)
        .setArraySize(s_ArraySize)
        .setDimension(TextureDimension::Texture2DArray)
        .setFormat(Format::BC1_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    const TextureHandle texture = device.createTexture(textureDesc);
    if(!texture)
        GTEST_SKIP() << "Block-compressed clear smoke: BC1_UNORM array texture creation failed.";

    const StagingTextureHandle readback = device.createStagingTexture(textureDesc, CpuAccessMode::Read);
    ASSERT_TRUE(readback);
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

    if((device.queryFormatSupport(Format::BC7_UNORM) & FormatSupport::Texture) == FormatSupport::Texture){
        TextureDesc unsupportedDesc = textureDesc;
        unsupportedDesc
            .setMipLevels(1u)
            .setArraySize(1u)
            .setDimension(TextureDimension::Texture2D)
            .setFormat(Format::BC7_UNORM)
        ;
        const TextureHandle unsupportedTexture = device.createTexture(unsupportedDesc);
        if(unsupportedTexture){
            const usize referencesBeforeReject = unsupportedTexture->getReferenceCount();
            commandList->open();
            ASSERT_FALSE(commandList->isRenderPassActive());
            commandList->clearTextureFloat(
                unsupportedTexture.get(),
                s_AllSubresources,
                Color(1.f, 0.f, 1.f, 1.f)
            );
            EXPECT_TRUE(commandList->commandRecordingFailed());
            EXPECT_FALSE(commandList->isRenderPassActive());
            EXPECT_EQ(unsupportedTexture->getReferenceCount(), referencesBeforeReject);
            commandList->close();
            EXPECT_FALSE(commandList->hasCommandBuffer());
            EXPECT_FALSE(submit().valid());
        }
    }

    TextureDesc& mutableTextureDesc = const_cast<TextureDesc&>(texture->getDescription());
    mutableTextureDesc.setSampleCount(2u);
    const usize referencesBeforeMultisampleReject = texture->getReferenceCount();
    commandList->open();
    ASSERT_FALSE(commandList->isRenderPassActive());
    commandList->clearTextureFloat(texture.get(), s_AllSubresources, Color(1.f, 0.f, 1.f, 1.f));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforeMultisampleReject);
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_FALSE(submit().valid());

    commandList->open();
    ASSERT_FALSE(commandList->isRenderPassActive());
    commandList->clearTextureBoxFloat(
        texture.get(),
        TextureSubresourceSet(0u, 1u, 0u, 1u),
        Box(4, 4, 1),
        Color(1.f, 0.f, 1.f, 1.f)
    );
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforeMultisampleReject);
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_FALSE(submit().valid());
    mutableTextureDesc.setSampleCount(1u);

    const TextureHandle renderTarget = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(renderTarget);
    const FramebufferHandle framebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(renderTarget.get())
    );
    ASSERT_TRUE(framebuffer);
    const usize referencesBeforeActiveRenderingReject = texture->getReferenceCount();
    for(const bool boundedClear : { false, true }){
        commandList->open();
        commandList->setGraphicsState(GraphicsState().setFramebuffer(framebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        if(boundedClear){
            commandList->clearTextureBoxFloat(
                texture.get(),
                TextureSubresourceSet(0u, 1u, 0u, 1u),
                Box(4, 4, 1),
                Color(1.f, 0.f, 1.f, 1.f)
            );
        }
        else
            commandList->clearTextureFloat(texture.get(), s_AllSubresources, Color(1.f, 0.f, 1.f, 1.f));
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->isRenderPassActive());
        EXPECT_EQ(texture->getReferenceCount(), referencesBeforeActiveRenderingReject);
        EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());
    }

    const TextureSubresourceSet clearedSubresources(0u, s_MipCount, 0u, s_ArraySize);
    // Fixed box coordinates clamp against monotonically shrinking mip extents, so an aligned first nonempty mip
    // cannot become unaligned later. This multi-mip rejection still proves validation precedes all state publication.
    const usize referencesBeforeAlignmentReject = texture->getReferenceCount();
    commandList->open();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_FALSE(commandList->isRenderPassActive());
    commandList->clearTextureBoxFloat(
        texture.get(),
        clearedSubresources,
        Box(0, 6, 0, static_cast<i32>(s_Height), 0, 1),
        Color(1.f, 0.f, 1.f, 1.f)
    );
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforeAlignmentReject);
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel)
            EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), arraySlice, mipLevel));
    }
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_FALSE(submit().valid());

    const usize referencesBeforeClear = texture->getReferenceCount();
    commandList->open();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_FALSE(commandList->isRenderPassActive());
    commandList->clearTextureFloat(
        texture.get(),
        clearedSubresources,
        Color(1.f, 0.f, 1.f, 1.f)
    );
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel){
            EXPECT_EQ(
                commandList->getTextureSubresourceState(texture.get(), arraySlice, mipLevel),
                ResourceStates::CopyDest
            );
        }
    }
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforeClear + 1u);

    CommandListResourceStateHandoff clearFinalStates(DescriptorBufferRoundTripTest::arena());
    commandList->close(&clearFinalStates);
    ASSERT_TRUE(clearFinalStates.valid());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const QueueSubmissionToken clearToken = submit();
    ASSERT_TRUE(clearToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    for(
        u32 retry = 0u;
        retry < 5000u && texture->getReferenceCount() != referencesBeforeClear;
        ++retry
    ){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforeClear);

    const CommandListHandle readbackCommandList = device.createCommandList();
    ASSERT_TRUE(readbackCommandList);
    readbackCommandList->open(&clearFinalStates);
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel){
            TextureSlice slice;
            slice.setMipLevel(mipLevel).setArraySlice(arraySlice);
            readbackCommandList->copyTexture(readback.get(), slice, texture.get(), slice);
        }
    }
    EXPECT_FALSE(readbackCommandList->commandRecordingFailed());
    readbackCommandList->close();
    CommandList* const readbackCommandLists[] = { readbackCommandList.get() };
    const QueueSubmissionToken readbackToken = device.executeCommandLists(
        readbackCommandLists,
        LengthOf(readbackCommandLists),
        readbackCommandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(readbackToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel){
            TextureSlice slice;
            slice.setMipLevel(mipLevel).setArraySlice(arraySlice);
            usize rowPitch = 0u;
            const u8* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
                readback.get(),
                slice,
                CpuAccessMode::Read,
                &rowPitch
            ));
            ASSERT_NE(readbackBytes, nullptr);

            const u32 mipWidth = Max(s_Width >> mipLevel, 1u);
            const u32 mipHeight = Max(s_Height >> mipLevel, 1u);
            const u32 blockColumnCount = DivideUp(mipWidth, s_BC1BlockWidth);
            const u32 blockRowCount = DivideUp(mipHeight, s_BC1BlockHeight);
            ASSERT_GE(rowPitch, static_cast<usize>(blockColumnCount * s_BC1BlockByteSize));
            for(u32 blockRow = 0u; blockRow < blockRowCount; ++blockRow){
                for(u32 blockColumn = 0u; blockColumn < blockColumnCount; ++blockColumn){
                    const u8* const blockBytes = readbackBytes
                        + static_cast<usize>(blockRow) * rowPitch
                        + static_cast<usize>(blockColumn) * s_BC1BlockByteSize
                    ;
                    for(u32 byteIndex = 0u; byteIndex < s_BC1BlockByteSize; ++byteIndex)
                        EXPECT_EQ(blockBytes[byteIndex], s_ExpectedBC1Block[byteIndex]);
                }
            }
            device.unmapStagingTexture(readback.get());
        }
    }
}


TEST_F(DescriptorBufferRoundTripTest, DepthStencilBoxPreflightRejectsAtomicallyAndRecoversWithReadback){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 s_Width = 4u;
    constexpr u32 s_Height = 4u;
    constexpr u32 s_MipCount = 2u;
    constexpr u32 s_ArraySize = 2u;
    constexpr Box s_ClearBox(1, 3, 1, 3, 0, 1);
    if((device.queryFormatSupport(Format::D32) & FormatSupport::DepthStencil) != FormatSupport::DepthStencil)
        GTEST_SKIP() << "Depth/stencil clear smoke: D32 depth attachments are unavailable.";

    const TextureDesc textureDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setMipLevels(s_MipCount)
        .setArraySize(s_ArraySize)
        .setDimension(TextureDimension::Texture2DArray)
        .setFormat(Format::D32)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
    ;
    const TextureHandle texture = device.createTexture(textureDesc);
    const StagingTextureHandle readback = device.createStagingTexture(textureDesc, CpuAccessMode::Read);
    if(!texture || !readback)
        GTEST_SKIP() << "Depth/stencil clear smoke: D32 array texture or readback creation failed.";

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
    const auto expectNoExplicitState = [&](){
        for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
            for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel)
                EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), arraySlice, mipLevel));
        }
    };
    const TextureSubresourceSet clearedSubresources(0u, s_MipCount, 0u, s_ArraySize);
    TextureDesc& mutableTextureDesc = const_cast<TextureDesc&>(texture->getDescription());

    const usize referencesBeforePatternReject = texture->getReferenceCount();
    mutableTextureDesc.setFormat(Format::RGBA8_UNORM);
    commandList->open();
    commandList->clearDepthStencilTextureBox(
        texture.get(),
        clearedSubresources,
        s_ClearBox,
        true,
        0.5f,
        false,
        0u
    );
    mutableTextureDesc.setFormat(Format::D32);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforePatternReject);
    expectNoExplicitState();
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_FALSE(submit().valid());

    const usize referencesBeforeEmptyClear = texture->getReferenceCount();
    mutableTextureDesc.setSampleCount(2u);
    commandList->open();
    commandList->clearDepthStencilTextureBox(
        texture.get(),
        clearedSubresources,
        Box(8, 9, 8, 9, 0, 1),
        true,
        0.5f,
        false,
        0u
    );
    mutableTextureDesc.setSampleCount(1u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforeEmptyClear);
    expectNoExplicitState();
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());

    const usize referencesBeforeMultisampleReject = texture->getReferenceCount();
    mutableTextureDesc.setSampleCount(2u);
    commandList->open();
    commandList->clearDepthStencilTextureBox(
        texture.get(),
        clearedSubresources,
        s_ClearBox,
        true,
        0.5f,
        false,
        0u
    );
    mutableTextureDesc.setSampleCount(1u);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(texture->getReferenceCount(), referencesBeforeMultisampleReject);
    expectNoExplicitState();
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_FALSE(submit().valid());

    const TextureHandle activeDepthTarget = device.createTexture(
        TextureDesc()
            .setWidth(s_Width)
            .setHeight(s_Height)
            .setFormat(Format::D32)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(activeDepthTarget);
    const FramebufferHandle activeDepthFramebuffer = device.createFramebuffer(
        FramebufferDesc().setDepthAttachment(activeDepthTarget.get())
    );
    ASSERT_TRUE(activeDepthFramebuffer);
    const usize referencesBeforeAttachmentReject = texture->getReferenceCount();
    for(const bool boundedClear : { false, true }){
        commandList->open();
        commandList->setGraphicsState(GraphicsState().setFramebuffer(activeDepthFramebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        if(boundedClear){
            commandList->clearDepthStencilTextureBox(
                texture.get(),
                clearedSubresources,
                s_ClearBox,
                true,
                0.5f,
                false,
                0u
            );
        }
        else{
            commandList->clearDepthStencilTexture(
                texture.get(),
                clearedSubresources,
                true,
                0.5f,
                false,
                0u
            );
        }
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->isRenderPassActive());
        EXPECT_EQ(texture->getReferenceCount(), referencesBeforeAttachmentReject);
        expectNoExplicitState();
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());
    }

    TextureDesc multisampleDesc = textureDesc;
    multisampleDesc
        .setMipLevels(1u)
        .setArraySize(1u)
        .setDimension(TextureDimension::Texture2DMS)
        .setSampleCount(2u)
    ;
    const TextureHandle multisampleTexture = device.createTexture(multisampleDesc);
    if(multisampleTexture){
        commandList->open();
        commandList->clearDepthStencilTexture(
            multisampleTexture.get(),
            s_AllSubresources,
            true,
            0.75f,
            false,
            0u
        );
        EXPECT_FALSE(commandList->commandRecordingFailed());
        EXPECT_EQ(
            commandList->getTextureSubresourceState(multisampleTexture.get(), 0u, 0u),
            ResourceStates::CopyDest
        );
        commandList->close();
        ASSERT_TRUE(commandList->hasCommandBuffer());
        ASSERT_TRUE(submit().valid());
        ASSERT_TRUE(device.waitForIdle());

        commandList->open();
        const usize referencesBeforeActualMultisampleReject = multisampleTexture->getReferenceCount();
        commandList->clearDepthStencilTextureBox(
            multisampleTexture.get(),
            s_AllSubresources,
            s_ClearBox,
            true,
            0.25f,
            false,
            0u
        );
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_EQ(multisampleTexture->getReferenceCount(), referencesBeforeActualMultisampleReject);
        EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(multisampleTexture.get(), 0u, 0u));
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());
    }

    CommandListResourceStateHandoff clearFinalStates(DescriptorBufferRoundTripTest::arena());
    commandList->open();
    commandList->clearDepthStencilTexture(
        texture.get(),
        clearedSubresources,
        true,
        2.0f,
        false,
        0u
    );
    commandList->clearDepthStencilTextureBox(
        texture.get(),
        clearedSubresources,
        s_ClearBox,
        true,
        0.25f,
        false,
        0u
    );
    ASSERT_FALSE(commandList->commandRecordingFailed());
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel){
            EXPECT_EQ(
                commandList->getTextureSubresourceState(texture.get(), arraySlice, mipLevel),
                ResourceStates::CopyDest
            );
        }
    }
    commandList->close(&clearFinalStates);
    ASSERT_TRUE(clearFinalStates.valid());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());

    if((device.queryFormatSupport(Format::D32S8) & FormatSupport::DepthStencil) == FormatSupport::DepthStencil){
        TextureDesc combinedDesc = textureDesc;
        combinedDesc.setFormat(Format::D32S8);
        const TextureHandle combinedTexture = device.createTexture(combinedDesc);
        if(combinedTexture){
            commandList->open();
            const usize referencesBeforeCombinedClear = combinedTexture->getReferenceCount();
            commandList->clearDepthStencilTextureBox(
                combinedTexture.get(),
                clearedSubresources,
                s_ClearBox,
                true,
                0.5f,
                true,
                0x5au
            );
            EXPECT_FALSE(commandList->commandRecordingFailed());
            EXPECT_EQ(combinedTexture->getReferenceCount(), referencesBeforeCombinedClear + 1u);
            for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
                for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel){
                    EXPECT_EQ(
                        commandList->getTextureSubresourceState(combinedTexture.get(), arraySlice, mipLevel),
                        ResourceStates::CopyDest
                    );
                }
            }
            commandList->close();
            ASSERT_TRUE(commandList->hasCommandBuffer());
            ASSERT_TRUE(submit().valid());
            ASSERT_TRUE(device.waitForIdle());
        }
    }

    const CommandListHandle readbackCommandList = device.createCommandList();
    ASSERT_TRUE(readbackCommandList);
    readbackCommandList->open(&clearFinalStates);
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel){
            TextureSlice slice;
            slice.setMipLevel(mipLevel).setArraySlice(arraySlice);
            readbackCommandList->copyTexture(readback.get(), slice, texture.get(), slice);
        }
    }
    ASSERT_FALSE(readbackCommandList->commandRecordingFailed());
    readbackCommandList->close();
    CommandList* const readbackCommandLists[] = { readbackCommandList.get() };
    const QueueSubmissionToken readbackToken = device.executeCommandLists(
        readbackCommandLists,
        LengthOf(readbackCommandLists),
        readbackCommandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(readbackToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        for(u32 mipLevel = 0u; mipLevel < s_MipCount; ++mipLevel){
            TextureSlice slice;
            slice.setMipLevel(mipLevel).setArraySlice(arraySlice);
            usize rowPitch = 0u;
            const u8* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
                readback.get(),
                slice,
                CpuAccessMode::Read,
                &rowPitch
            ));
            ASSERT_NE(readbackBytes, nullptr);

            const u32 mipWidth = Max(s_Width >> mipLevel, 1u);
            const u32 mipHeight = Max(s_Height >> mipLevel, 1u);
            ASSERT_GE(rowPitch, static_cast<usize>(mipWidth) * sizeof(f32));
            for(u32 y = 0u; y < mipHeight; ++y){
                for(u32 x = 0u; x < mipWidth; ++x){
                    f32 actualDepth = 0.0f;
                    const u8* const depthBytes = readbackBytes
                        + static_cast<usize>(y) * rowPitch
                        + static_cast<usize>(x) * sizeof(f32)
                    ;
                    NWB_MEMCPY(&actualDepth, sizeof(actualDepth), depthBytes, sizeof(actualDepth));
                    const bool insideClear = x >= 1u && x < Min(3u, mipWidth) && y >= 1u && y < Min(3u, mipHeight);
                    EXPECT_FLOAT_EQ(actualDepth, insideClear ? 0.25f : 1.0f);
                }
            }
            device.unmapStagingTexture(readback.get());
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

