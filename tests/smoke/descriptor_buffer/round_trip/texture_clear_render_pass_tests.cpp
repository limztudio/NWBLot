// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, TextureClearPreflightRejectsAtomicallyAndRecoversWithReadback){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 s_Width = 4u;
    constexpr u32 s_Height = 4u;
    constexpr Box s_PartialBox(1, 3, 1, 3, 0, 1);
    constexpr TextureSubresourceSet s_ActiveSubresources(0u, 1u, 0u, 1u);
    constexpr TextureSubresourceSet s_InactiveMipSubresources(1u, 1u, 0u, 1u);
    constexpr TextureSubresourceSet s_EmptySubresources(0u, 0u, 0u, 1u);

    const TextureDesc colorDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    const TextureHandle colorTexture = device.createTexture(colorDesc);
    const StagingTextureHandle colorReadback = device.createStagingTexture(colorDesc, CpuAccessMode::Read);
    const TextureHandle integerTexture = device.createTexture(
        TextureDesc()
            .setWidth(s_Width)
            .setHeight(s_Height)
            .setFormat(Format::RGBA8_UINT)
            .setInitialState(ResourceStates::Common)
    );
    const TextureHandle activeColorTexture = device.createTexture(
        TextureDesc()
            .setWidth(s_Width)
            .setHeight(s_Height)
            .setMipLevels(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(colorTexture);
    ASSERT_TRUE(colorReadback);
    ASSERT_TRUE(integerTexture);
    ASSERT_TRUE(activeColorTexture);

    const FramebufferHandle activeColorFramebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(activeColorTexture.get(), s_ActiveSubresources)
    );
    ASSERT_TRUE(activeColorFramebuffer);

    TextureHandle depthTexture;
    StagingTextureHandle depthReadback;
    FramebufferHandle depthFramebuffer;
    if((device.queryFormatSupport(Format::D32) & FormatSupport::DepthStencil) == FormatSupport::DepthStencil){
        depthTexture = device.createTexture(
            TextureDesc()
                .setWidth(s_Width)
                .setHeight(s_Height)
                .setFormat(Format::D32)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
        );
        if(depthTexture){
            depthReadback = device.createStagingTexture(depthTexture->getDescription(), CpuAccessMode::Read);
            depthFramebuffer = device.createFramebuffer(FramebufferDesc().setDepthAttachment(depthTexture.get()));
            ASSERT_TRUE(depthReadback);
            ASSERT_TRUE(depthFramebuffer);
        }
    }

    TextureHandle multisampleTexture;
    multisampleTexture = device.createTexture(
        TextureDesc()
            .setWidth(s_Width)
            .setHeight(s_Height)
            .setDimension(TextureDimension::Texture2DMS)
            .setFormat(Format::RGBA8_UNORM)
            .setSampleCount(2u)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    const TextureHandle foreignColorTexture = foreignDevice.createTexture(colorDesc);
    ASSERT_TRUE(foreignColorTexture);
    TextureHandle foreignDepthTexture;
    if(
        (foreignDevice.queryFormatSupport(Format::D32) & FormatSupport::DepthStencil)
        == FormatSupport::DepthStencil
    ){
        foreignDepthTexture = foreignDevice.createTexture(
            TextureDesc()
                .setWidth(s_Width)
                .setHeight(s_Height)
                .setFormat(Format::D32)
                .setInitialState(ResourceStates::Common)
        );
    }

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

    enum class Operation : u8{
        NullColorFull,
        NullColorBox,
        EmptyColorFullRange,
        OutOfRangeColorBox,
        FloatOnIntegerFull,
        IntegerOnFloatBox,
        ColorOnDepthFull,
        NullDepthFull,
        NullDepthBox,
        EmptyDepthFullRange,
        StencilOnDepthOnlyFull,
        DepthOnColorFull,
        ActiveUnrelatedFull,
        ActiveUnrelatedBox,
        ActiveSubresource,
        ActiveReadOnly,
        ActiveOutOfArea,
        PartialMultisampleBox,
        ForeignColorFull,
        ForeignColorBox,
        ForeignDepthFull,
        ForeignDepthBox,
    };
    constexpr Operation s_Operations[] = {
        Operation::NullColorFull,
        Operation::NullColorBox,
        Operation::EmptyColorFullRange,
        Operation::OutOfRangeColorBox,
        Operation::FloatOnIntegerFull,
        Operation::IntegerOnFloatBox,
        Operation::ColorOnDepthFull,
        Operation::NullDepthFull,
        Operation::NullDepthBox,
        Operation::EmptyDepthFullRange,
        Operation::StencilOnDepthOnlyFull,
        Operation::DepthOnColorFull,
        Operation::ActiveUnrelatedFull,
        Operation::ActiveUnrelatedBox,
        Operation::ActiveSubresource,
        Operation::ActiveReadOnly,
        Operation::ActiveOutOfArea,
        Operation::PartialMultisampleBox,
        Operation::ForeignColorFull,
        Operation::ForeignColorBox,
        Operation::ForeignDepthFull,
        Operation::ForeignDepthBox,
    };

    FramebufferDesc& mutableFramebufferDesc = const_cast<FramebufferDesc&>(
        activeColorFramebuffer->getDescription()
    );
    FramebufferInfoEx& mutableFramebufferInfo = const_cast<FramebufferInfoEx&>(
        activeColorFramebuffer->getFramebufferInfo()
    );
    for(const Operation operation : s_Operations){
        SCOPED_TRACE(static_cast<u32>(operation));
        Texture* target = nullptr;
        bool activeOperation = false;
        switch(operation){
        case Operation::FloatOnIntegerFull:
            target = integerTexture.get();
            break;
        case Operation::ColorOnDepthFull:
        case Operation::EmptyDepthFullRange:
        case Operation::StencilOnDepthOnlyFull:
            if(!depthTexture)
                continue;
            target = depthTexture.get();
            break;
        case Operation::ActiveSubresource:
        case Operation::ActiveReadOnly:
        case Operation::ActiveOutOfArea:
            target = activeColorTexture.get();
            activeOperation = true;
            break;
        case Operation::ActiveUnrelatedFull:
        case Operation::ActiveUnrelatedBox:
            target = colorTexture.get();
            activeOperation = true;
            break;
        case Operation::PartialMultisampleBox:
            if(!multisampleTexture)
                continue;
            target = multisampleTexture.get();
            break;
        case Operation::ForeignColorFull:
        case Operation::ForeignColorBox:
            target = foreignColorTexture.get();
            break;
        case Operation::ForeignDepthFull:
        case Operation::ForeignDepthBox:
            if(!foreignDepthTexture)
                continue;
            target = foreignDepthTexture.get();
            break;
        case Operation::EmptyColorFullRange:
        case Operation::OutOfRangeColorBox:
        case Operation::IntegerOnFloatBox:
        case Operation::DepthOnColorFull:
            target = colorTexture.get();
            break;
        case Operation::NullColorFull:
        case Operation::NullColorBox:
        case Operation::NullDepthFull:
        case Operation::NullDepthBox:
            break;
        }

        commandList->open();
        ASSERT_FALSE(commandList->commandRecordingFailed());
        if(activeOperation){
            commandList->setGraphicsState(GraphicsState().setFramebuffer(activeColorFramebuffer.get()));
            ASSERT_FALSE(commandList->commandRecordingFailed());
            ASSERT_TRUE(commandList->isRenderPassActive());
        }

        constexpr u32 s_MaxTrackedMipCount = 2u;
        bool explicitStateBefore[s_MaxTrackedMipCount] = {};
        ResourceStates::Mask stateBefore[s_MaxTrackedMipCount] = {};
        const u32 trackedMipCount = target
            ? Min(target->getDescription().mipLevels, s_MaxTrackedMipCount)
            : 0u
        ;
        const usize referencesBefore = target ? target->getReferenceCount() : 0u;
        for(u32 mipLevel = 0u; mipLevel < trackedMipCount; ++mipLevel){
            explicitStateBefore[mipLevel] = commandList->hasExplicitTextureSubresourceState(
                target,
                0u,
                mipLevel
            );
            if(explicitStateBefore[mipLevel])
                stateBefore[mipLevel] = commandList->getTextureSubresourceState(target, 0u, mipLevel);
        }

        const bool mutateReadOnly = operation == Operation::ActiveReadOnly;
        const bool mutateRenderArea = operation == Operation::ActiveOutOfArea;
        const u32 originalFramebufferWidth = mutableFramebufferInfo.width;
        const u32 originalFramebufferHeight = mutableFramebufferInfo.height;
        if(mutateReadOnly)
            mutableFramebufferDesc.colorAttachments[0u].isReadOnly = true;
        if(mutateRenderArea){
            mutableFramebufferInfo.width = 2u;
            mutableFramebufferInfo.height = 2u;
        }

        switch(operation){
        case Operation::NullColorFull:
            commandList->clearTextureFloat(nullptr, s_AllSubresources, Color(0.f));
            break;
        case Operation::NullColorBox:
            commandList->clearTextureBoxFloat(nullptr, s_AllSubresources, s_PartialBox, Color(0.f));
            break;
        case Operation::EmptyColorFullRange:
            commandList->clearTextureFloat(colorTexture.get(), s_EmptySubresources, Color(0.f));
            break;
        case Operation::OutOfRangeColorBox:
            commandList->clearTextureBoxFloat(
                colorTexture.get(),
                s_InactiveMipSubresources,
                s_PartialBox,
                Color(0.f)
            );
            break;
        case Operation::FloatOnIntegerFull:
            commandList->clearTextureFloat(integerTexture.get(), s_AllSubresources, Color(0.f));
            break;
        case Operation::IntegerOnFloatBox:
            commandList->clearTextureBoxUInt(colorTexture.get(), s_AllSubresources, s_PartialBox, UIntColor(1u));
            break;
        case Operation::ColorOnDepthFull:
            commandList->clearTextureFloat(depthTexture.get(), s_AllSubresources, Color(0.f));
            break;
        case Operation::NullDepthFull:
            commandList->clearDepthStencilTexture(nullptr, s_AllSubresources, true, 0.5f, false, 0u);
            break;
        case Operation::NullDepthBox:
            commandList->clearDepthStencilTextureBox(
                nullptr,
                s_AllSubresources,
                s_PartialBox,
                true,
                0.5f,
                false,
                0u
            );
            break;
        case Operation::EmptyDepthFullRange:
            commandList->clearDepthStencilTexture(
                depthTexture.get(),
                s_EmptySubresources,
                true,
                0.5f,
                false,
                0u
            );
            break;
        case Operation::StencilOnDepthOnlyFull:
            commandList->clearDepthStencilTexture(
                depthTexture.get(),
                s_AllSubresources,
                false,
                Limit<f32>::s_QuietNaN,
                true,
                0x5au
            );
            break;
        case Operation::DepthOnColorFull:
            commandList->clearDepthStencilTexture(
                colorTexture.get(),
                s_AllSubresources,
                true,
                0.5f,
                false,
                0u
            );
            break;
        case Operation::ActiveUnrelatedFull:
            commandList->clearTextureFloat(colorTexture.get(), s_AllSubresources, Color(0.f));
            break;
        case Operation::ActiveUnrelatedBox:
            commandList->clearTextureBoxFloat(
                colorTexture.get(),
                s_AllSubresources,
                s_PartialBox,
                Color(0.f)
            );
            break;
        case Operation::ActiveSubresource:
            commandList->clearTextureFloat(activeColorTexture.get(), s_InactiveMipSubresources, Color(0.f));
            break;
        case Operation::ActiveReadOnly:
        case Operation::ActiveOutOfArea:
            commandList->clearTextureFloat(activeColorTexture.get(), s_ActiveSubresources, Color(0.f));
            break;
        case Operation::PartialMultisampleBox:
            commandList->clearTextureBoxFloat(
                multisampleTexture.get(),
                s_AllSubresources,
                s_PartialBox,
                Color(0.5f)
            );
            break;
        case Operation::ForeignColorFull:
            commandList->clearTextureFloat(foreignColorTexture.get(), s_AllSubresources, Color(0.f));
            break;
        case Operation::ForeignColorBox:
            commandList->clearTextureBoxFloat(
                foreignColorTexture.get(),
                s_AllSubresources,
                s_PartialBox,
                Color(0.f)
            );
            break;
        case Operation::ForeignDepthFull:
            commandList->clearDepthStencilTexture(
                foreignDepthTexture.get(),
                s_AllSubresources,
                true,
                0.5f,
                false,
                0u
            );
            break;
        case Operation::ForeignDepthBox:
            commandList->clearDepthStencilTextureBox(
                foreignDepthTexture.get(),
                s_AllSubresources,
                s_PartialBox,
                true,
                0.5f,
                false,
                0u
            );
            break;
        }

        if(mutateReadOnly)
            mutableFramebufferDesc.colorAttachments[0u].isReadOnly = false;
        if(mutateRenderArea){
            mutableFramebufferInfo.width = originalFramebufferWidth;
            mutableFramebufferInfo.height = originalFramebufferHeight;
        }

        EXPECT_TRUE(commandList->commandRecordingFailed());
        if(target){
            EXPECT_EQ(target->getReferenceCount(), referencesBefore);
            for(u32 mipLevel = 0u; mipLevel < trackedMipCount; ++mipLevel){
                EXPECT_EQ(
                    commandList->hasExplicitTextureSubresourceState(target, 0u, mipLevel),
                    explicitStateBefore[mipLevel]
                );
                if(explicitStateBefore[mipLevel]){
                    EXPECT_EQ(
                        commandList->getTextureSubresourceState(target, 0u, mipLevel),
                        stateBefore[mipLevel]
                    );
                }
            }
        }
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());
        for(u32 mipLevel = 0u; mipLevel < trackedMipCount; ++mipLevel)
            EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(target, 0u, mipLevel));
    }

    commandList->open();
    const usize referencesBeforeNoOps = colorTexture->getReferenceCount();
    commandList->clearTextureBoxFloat(
        nullptr,
        s_AllSubresources,
        Box(0, 0, 0, 0, 0, 0),
        Color(0.f)
    );
    commandList->clearDepthStencilTexture(
        nullptr,
        s_AllSubresources,
        false,
        Limit<f32>::s_QuietNaN,
        false,
        0u
    );
    commandList->clearDepthStencilTextureBox(
        nullptr,
        s_AllSubresources,
        Box(0, 0, 0, 0, 0, 0),
        true,
        0.5f,
        false,
        0u
    );
    commandList->clearTextureBoxFloat(
        colorTexture.get(),
        s_AllSubresources,
        Box(8, 9, 8, 9, 0, 1),
        Color(0.f)
    );
    const usize multisampleReferencesBeforeNoOp = multisampleTexture
        ? multisampleTexture->getReferenceCount()
        : 0u
    ;
    if(multisampleTexture){
        commandList->clearTextureBoxFloat(
            multisampleTexture.get(),
            s_AllSubresources,
            Box(8, 9, 8, 9, 0, 1),
            Color(0.f)
        );
    }
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(colorTexture->getReferenceCount(), referencesBeforeNoOps);
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(colorTexture.get(), 0u, 0u));
    if(multisampleTexture){
        EXPECT_EQ(multisampleTexture->getReferenceCount(), multisampleReferencesBeforeNoOp);
        EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(multisampleTexture.get(), 0u, 0u));
    }
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());

    if(multisampleTexture){
        commandList->open();
        const usize referencesBeforeFullMultisampleClear = multisampleTexture->getReferenceCount();
        commandList->clearTextureFloat(multisampleTexture.get(), s_AllSubresources, Color(0.25f));
        EXPECT_FALSE(commandList->commandRecordingFailed());
        EXPECT_EQ(multisampleTexture->getReferenceCount(), referencesBeforeFullMultisampleClear + 1u);
        EXPECT_EQ(
            commandList->getTextureSubresourceState(multisampleTexture.get(), 0u, 0u),
            ResourceStates::CopyDest
        );
        commandList->close();
        ASSERT_TRUE(commandList->hasCommandBuffer());
        ASSERT_TRUE(submit().valid());
        ASSERT_TRUE(device.waitForIdle());
    }

    if((device.queryFormatSupport(Format::D32S8) & FormatSupport::DepthStencil) == FormatSupport::DepthStencil){
        const TextureHandle depthStencilTexture = device.createTexture(
            TextureDesc()
                .setWidth(s_Width)
                .setHeight(s_Height)
                .setFormat(Format::D32S8)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
        );
        if(depthStencilTexture){
            commandList->open();
            commandList->clearDepthStencilTexture(
                depthStencilTexture.get(),
                s_AllSubresources,
                false,
                Limit<f32>::s_QuietNaN,
                true,
                0x5au
            );
            EXPECT_FALSE(commandList->commandRecordingFailed());
            commandList->close();
            ASSERT_TRUE(commandList->hasCommandBuffer());
            ASSERT_TRUE(submit().valid());
            ASSERT_TRUE(device.waitForIdle());
        }
    }

    if(depthTexture && depthReadback && depthFramebuffer){
        commandList->open();
        commandList->setGraphicsState(GraphicsState().setFramebuffer(depthFramebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        commandList->clearDepthStencilTexture(
            depthTexture.get(),
            s_AllSubresources,
            true,
            2.0f,
            false,
            0u
        );
        commandList->clearDepthStencilTextureBox(
            depthTexture.get(),
            s_AllSubresources,
            s_PartialBox,
            true,
            Limit<f32>::s_QuietNaN,
            false,
            0u
        );
        commandList->copyTexture(depthReadback.get(), TextureSlice(), depthTexture.get(), TextureSlice());
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        ASSERT_TRUE(commandList->hasCommandBuffer());
        ASSERT_TRUE(submit().valid());
        ASSERT_TRUE(device.waitForIdle());

        usize depthRowPitch = 0u;
        const u8* const depthReadbackBytes = static_cast<const u8*>(device.mapStagingTexture(
            depthReadback.get(),
            TextureSlice(),
            CpuAccessMode::Read,
            &depthRowPitch
        ));
        ASSERT_NE(depthReadbackBytes, nullptr);
        ASSERT_GE(depthRowPitch, static_cast<usize>(s_Width) * sizeof(f32));
        for(u32 y = 0u; y < s_Height; ++y){
            for(u32 x = 0u; x < s_Width; ++x){
                f32 actualDepth = 0.0f;
                const u8* const depthBytes = depthReadbackBytes
                    + static_cast<usize>(y) * depthRowPitch
                    + static_cast<usize>(x) * sizeof(f32)
                ;
                NWB_MEMCPY(&actualDepth, sizeof(actualDepth), depthBytes, sizeof(actualDepth));
                const bool insideClear = x >= 1u && x < 3u && y >= 1u && y < 3u;
                EXPECT_FLOAT_EQ(actualDepth, insideClear ? 0.0f : 1.0f);
            }
        }
        device.unmapStagingTexture(depthReadback.get());
    }

    commandList->open();
    commandList->clearTextureFloat(colorTexture.get(), s_AllSubresources, Color(0.f));
    commandList->clearTextureBoxFloat(
        colorTexture.get(),
        s_AllSubresources,
        s_PartialBox,
        Color(0.25f, 0.5f, 0.75f, 1.f)
    );
    commandList->copyTexture(colorReadback.get(), TextureSlice(), colorTexture.get(), TextureSlice());
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());

    usize rowPitch = 0u;
    const u8* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        colorReadback.get(),
        TextureSlice(),
        CpuAccessMode::Read,
        &rowPitch
    ));
    ASSERT_NE(readbackBytes, nullptr);
    ASSERT_GE(rowPitch, static_cast<usize>(s_Width * 4u));
    constexpr u8 s_ExpectedClear[] = { 64u, 128u, 191u, 255u };
    for(u32 y = 0u; y < s_Height; ++y){
        for(u32 x = 0u; x < s_Width; ++x){
            const bool insideClear = x >= 1u && x < 3u && y >= 1u && y < 3u;
            const u8* const pixel = readbackBytes + static_cast<usize>(y) * rowPitch + x * 4u;
            for(u32 channel = 0u; channel < 4u; ++channel)
                EXPECT_EQ(pixel[channel], insideClear ? s_ExpectedClear[channel] : 0u);
        }
    }
    device.unmapStagingTexture(colorReadback.get());
}


TEST_F(DescriptorBufferRoundTripTest, ExplicitRenderPassClearSurvivesBarrierResumeAndStoresAttachments){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 s_Width = 6u;
    constexpr u32 s_Height = 4u;
    constexpr FormatSupport::Mask s_RequiredColorSupport = FormatSupport::Texture
        | FormatSupport::RenderTarget
    ;
    if(
        (device.queryFormatSupport(Format::RGBA8_UNORM) & s_RequiredColorSupport)
            != s_RequiredColorSupport
        || (device.queryFormatSupport(Format::D32) & FormatSupport::DepthStencil)
            != FormatSupport::DepthStencil
    )
        GTEST_SKIP() << "Explicit render-pass clear smoke: color or D32 render targets are unavailable.";

    const TextureDesc colorDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
    ;
    const TextureDesc depthDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setFormat(Format::D32)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
    ;
    const TextureHandle colorTexture = device.createTexture(colorDesc);
    const TextureHandle depthTexture = device.createTexture(depthDesc);
    const StagingTextureHandle colorReadback = device.createStagingTexture(colorDesc, CpuAccessMode::Read);
    const StagingTextureHandle depthReadback = device.createStagingTexture(depthDesc, CpuAccessMode::Read);
    if(!colorTexture || !depthTexture || !colorReadback || !depthReadback)
        GTEST_SKIP() << "Explicit render-pass clear smoke: attachment or readback creation failed.";

    const FramebufferHandle framebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(colorTexture.get()).setDepthAttachment(depthTexture.get())
    );
    const BufferHandle barrierBuffer = device.createBuffer(
        BufferDesc().setByteSize(sizeof(u32) * 4u).setInitialState(ResourceStates::Common).setKeepInitialState(true)
    );
    const CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(framebuffer);
    ASSERT_TRUE(barrierBuffer);
    ASSERT_TRUE(commandList);

    RenderPassParameters params;
    params.colorAttachmentActions[0u].loadAction = RenderPassLoadAction::Clear;
    params.colorClearValues[0u] = Color(0.25f, 0.5f, 0.75f, 1.0f);
    params.depthAttachmentActions.loadAction = RenderPassLoadAction::Clear;
    params.depthClearValue = 0.75f;
    constexpr Box s_FirstPartialBox(0, 2, 1, 3, 0, 1);
    constexpr Box s_SecondPartialBox(4, 6, 1, 3, 0, 1);

    commandList->open();
    commandList->beginTrackingBufferState(barrierBuffer.get(), ResourceStates::Common);
    commandList->beginRenderPass(framebuffer.get(), params);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->isRenderPassActive());
    EXPECT_EQ(
        commandList->getTextureSubresourceState(colorTexture.get(), 0u, 0u),
        ResourceStates::RenderTarget
    );
    EXPECT_EQ(
        commandList->getTextureSubresourceState(depthTexture.get(), 0u, 0u),
        ResourceStates::DepthWrite
    );

    commandList->clearTextureBoxFloat(
        colorTexture.get(),
        s_AllSubresources,
        s_FirstPartialBox,
        Color(1.0f, 0.0f, 0.0f, 1.0f)
    );
    commandList->clearDepthStencilTextureBox(
        depthTexture.get(),
        s_AllSubresources,
        s_FirstPartialBox,
        true,
        0.25f,
        false,
        0u
    );
    ASSERT_TRUE(commandList->isRenderPassActive());

    commandList->setBufferState(barrierBuffer.get(), ResourceStates::CopyDest);
    commandList->commitBarriers();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->isRenderPassActive());

    commandList->clearTextureBoxFloat(
        colorTexture.get(),
        s_AllSubresources,
        s_SecondPartialBox,
        Color(0.0f, 1.0f, 0.0f, 1.0f)
    );
    commandList->clearDepthStencilTextureBox(
        depthTexture.get(),
        s_AllSubresources,
        s_SecondPartialBox,
        true,
        0.5f,
        false,
        0u
    );
    ASSERT_TRUE(commandList->isRenderPassActive());
    commandList->endRenderPass();
    commandList->copyTexture(colorReadback.get(), TextureSlice(), colorTexture.get(), TextureSlice());
    commandList->copyTexture(depthReadback.get(), TextureSlice(), depthTexture.get(), TextureSlice());
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    CommandList* const commandLists[] = { commandList.get() };
    ASSERT_TRUE(device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        commandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());

    usize colorRowPitch = 0u;
    const u8* const colorBytes = static_cast<const u8*>(device.mapStagingTexture(
        colorReadback.get(),
        TextureSlice(),
        CpuAccessMode::Read,
        &colorRowPitch
    ));
    usize depthRowPitch = 0u;
    const u8* const depthBytes = static_cast<const u8*>(device.mapStagingTexture(
        depthReadback.get(),
        TextureSlice(),
        CpuAccessMode::Read,
        &depthRowPitch
    ));
    ASSERT_NE(colorBytes, nullptr);
    ASSERT_NE(depthBytes, nullptr);
    ASSERT_GE(colorRowPitch, static_cast<usize>(s_Width) * 4u);
    ASSERT_GE(depthRowPitch, static_cast<usize>(s_Width) * sizeof(f32));
    constexpr u8 s_BaseColor[] = { 64u, 128u, 191u, 255u };
    constexpr u8 s_FirstColor[] = { 255u, 0u, 0u, 255u };
    constexpr u8 s_SecondColor[] = { 0u, 255u, 0u, 255u };
    for(u32 y = 0u; y < s_Height; ++y){
        for(u32 x = 0u; x < s_Width; ++x){
            const bool insideFirst = x < 2u && y >= 1u && y < 3u;
            const bool insideSecond = x >= 4u && y >= 1u && y < 3u;
            const u8* const expectedColor = insideFirst
                ? s_FirstColor
                : insideSecond ? s_SecondColor : s_BaseColor
            ;
            const u8* const pixel = colorBytes + static_cast<usize>(y) * colorRowPitch + x * 4u;
            for(u32 channel = 0u; channel < 4u; ++channel)
                EXPECT_EQ(pixel[channel], expectedColor[channel]);

            f32 actualDepth = 0.0f;
            const u8* const pixelDepth = depthBytes
                + static_cast<usize>(y) * depthRowPitch
                + static_cast<usize>(x) * sizeof(f32)
            ;
            NWB_MEMCPY(&actualDepth, sizeof(actualDepth), pixelDepth, sizeof(actualDepth));
            EXPECT_FLOAT_EQ(actualDepth, insideFirst ? 0.25f : insideSecond ? 0.5f : 0.75f);
        }
    }
    device.unmapStagingTexture(depthReadback.get());
    device.unmapStagingTexture(colorReadback.get());
}


TEST_F(DescriptorBufferRoundTripTest, TransferCommandsEndRenderingAndAttachmentTransitionsDoNotResumeIt){
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureHandle renderTarget = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    const TextureHandle unrelatedTexture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(unrelatedTexture);
    const FramebufferDesc framebufferDesc = FramebufferDesc().addColorAttachment(renderTarget.get());
    const FramebufferHandle framebuffer = device.createFramebuffer(framebufferDesc);
    ASSERT_TRUE(framebuffer);

    const BufferHandle source = device.createBuffer(
        BufferDesc().setByteSize(sizeof(u32) * 4u).setInitialState(ResourceStates::Common).setKeepInitialState(true)
    );
    const BufferHandle destination = device.createBuffer(
        BufferDesc().setByteSize(sizeof(u32) * 4u).setInitialState(ResourceStates::Common).setKeepInitialState(true)
    );
    ASSERT_TRUE(source);
    ASSERT_TRUE(destination);
    constexpr u32 s_Words[] = { 7u, 8u, 9u, 10u };

    enum class Operation : u8{
        Write,
        Fill,
        Copy,
    };
    for(const Operation operation : { Operation::Write, Operation::Fill, Operation::Copy }){
        auto commandList = device.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        if(operation == Operation::Copy){
            commandList->beginTrackingBufferState(source.get(), ResourceStates::CopySource);
            commandList->beginTrackingBufferState(destination.get(), ResourceStates::CopyDest);
        }
        else
            commandList->beginTrackingBufferState(destination.get(), ResourceStates::CopyDest);

        commandList->setGraphicsState(GraphicsState().setFramebuffer(framebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        switch(operation){
        case Operation::Write:
            commandList->writeBuffer(destination.get(), s_Words, sizeof(s_Words));
            break;
        case Operation::Fill:
            commandList->clearBufferUInt(destination.get(), 0x01020304u);
            break;
        case Operation::Copy:
            commandList->copyBuffer(destination.get(), 0u, source.get(), 0u, sizeof(s_Words));
            break;
        }
        EXPECT_FALSE(commandList->isRenderPassActive());
        EXPECT_FALSE(commandList->commandRecordingFailed());
        commandList->close();

        CommandList* const commandLists[] = { commandList.get() };
        EXPECT_TRUE(device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        ).valid());
    }

    auto barrierList = device.createCommandList();
    ASSERT_TRUE(barrierList);
    barrierList->open();
    barrierList->beginTrackingBufferState(destination.get(), ResourceStates::Common);
    barrierList->setGraphicsState(GraphicsState().setFramebuffer(framebuffer.get()));
    ASSERT_TRUE(barrierList->isRenderPassActive());
    barrierList->setBufferState(destination.get(), ResourceStates::CopyDest);
    EXPECT_TRUE(barrierList->isRenderPassActive());
    barrierList->setTextureState(unrelatedTexture.get(), s_AllSubresources, ResourceStates::CopyDest);
    EXPECT_TRUE(barrierList->isRenderPassActive());
    barrierList->setTextureState(renderTarget.get(), s_AllSubresources, ResourceStates::CopySource);
    EXPECT_FALSE(barrierList->isRenderPassActive());
    barrierList->endRenderPass();
    barrierList->endRenderPass();
    EXPECT_FALSE(barrierList->commandRecordingFailed());
    barrierList->close();
    CommandList* const barrierLists[] = { barrierList.get() };
    EXPECT_TRUE(device.executeCommandLists(
        barrierLists,
        LengthOf(barrierLists),
        barrierList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

