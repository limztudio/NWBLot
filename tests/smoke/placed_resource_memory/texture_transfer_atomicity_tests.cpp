// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture-transfer ingress preflight, render-pass atomicity, and sticky-rejection coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_texture_transfer_atomicity_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FirstResource, typename SecondResource, typename RecordOperation, typename VerifyState>
static void ExpectAtomicTransferRejection(
    GraphicsBackend::Device& device,
    Framebuffer& framebuffer,
    Texture& renderTarget,
    FirstResource& firstResource,
    SecondResource& secondResource,
    RecordOperation&& recordOperation,
    VerifyState&& verifyState
){
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(&framebuffer));
    ASSERT_TRUE(commandList->isRenderPassActive());
    ASSERT_FALSE(commandList->commandRecordingFailed());

    const u32 firstReferences = firstResource.getReferenceCount();
    const u32 secondReferences = secondResource.getReferenceCount();
    recordOperation(*commandList);

    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->isRenderPassActive());
    EXPECT_EQ(
        commandList->getTextureSubresourceState(&renderTarget, 0u, 0u),
        ResourceStates::RenderTarget
    );
    EXPECT_EQ(firstResource.getReferenceCount(), firstReferences);
    EXPECT_EQ(secondResource.getReferenceCount(), secondReferences);
    verifyState(*commandList);

    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());

    commandList->open();
    EXPECT_TRUE(commandList->isRecording());
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_TRUE(commandList->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


TEST(TextureTransferAlignmentTest, ComputesDefaultAndFormatBlockLeastCommonMultiple){
    u32 alignment = 0x5a5a5a5au;
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::TryComputeUploadSuballocationAlignment(0u, alignment));
    EXPECT_EQ(alignment, 0u);

    EXPECT_TRUE(GraphicsBackend::VulkanDetail::TryComputeUploadSuballocationAlignment(4u, alignment));
    EXPECT_EQ(alignment, 256u);

    EXPECT_TRUE(GraphicsBackend::VulkanDetail::TryComputeUploadSuballocationAlignment(12u, alignment));
    EXPECT_EQ(alignment, 768u);

    EXPECT_FALSE(GraphicsBackend::VulkanDetail::TryComputeUploadSuballocationAlignment(Limit<u32>::s_Max, alignment));
    EXPECT_EQ(alignment, 0u);
}


class TextureTransferAtomicityTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->setTransferQueueEnabled(true)){
            GTEST_SKIP() << "Texture-transfer atomicity: transfer-queue configuration is unavailable.";
            return;
        }
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Texture-transfer atomicity: no validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "texture-transfer atomicity tests emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static TextureDesc ordinaryTextureDesc(){
        return TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
        ;
    }

    [[nodiscard]] static TextureHandle createUnboundTexture(){
        TextureDesc desc = ordinaryTextureDesc();
        desc.isVirtual = true;
        return device().createTexture(desc);
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool TextureTransferAtomicityTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> TextureTransferAtomicityTest::s_scope;
Optional<CapturingLogger> TextureTransferAtomicityTest::s_logger;
Optional<Common::LoggerRegistrationGuard> TextureTransferAtomicityTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextureTransferAtomicityTest, UnboundCopyStagingAndWriteOperandsRejectAtomically){
    TextureDesc renderTargetDesc = ordinaryTextureDesc();
    renderTargetDesc.setInRenderTarget(true);
    TextureHandle renderTarget = device().createTexture(renderTargetDesc);
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(renderTarget.get())
    );
    TextureHandle copySource = device().createTexture(ordinaryTextureDesc());
    TextureHandle copyDestination = device().createTexture(ordinaryTextureDesc());
    TextureHandle unboundSource = createUnboundTexture();
    TextureHandle unboundDestination = createUnboundTexture();
    StagingTextureHandle readback = device().createStagingTexture(ordinaryTextureDesc(), CpuAccessMode::Read);
    StagingTextureHandle upload = device().createStagingTexture(ordinaryTextureDesc(), CpuAccessMode::Write);
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(framebuffer);
    ASSERT_TRUE(copySource);
    ASSERT_TRUE(copyDestination);
    ASSERT_TRUE(unboundSource);
    ASSERT_TRUE(unboundDestination);
    ASSERT_TRUE(readback);
    ASSERT_TRUE(upload);
    ASSERT_FALSE(device().isTextureReadyForGpuUse(unboundSource.get()));
    ASSERT_FALSE(device().isTextureReadyForGpuUse(unboundDestination.get()));

    {
        SCOPED_TRACE("direct copy unbound destination");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *copySource,
            *unboundDestination,
            [&](CommandList& commandList){
                commandList.copyTexture(
                    unboundDestination.get(),
                    TextureSlice{},
                    copySource.get(),
                    TextureSlice{}
                );
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(copySource.get(), 0u, 0u));
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(unboundDestination.get(), 0u, 0u));
            }
        );
    }
    {
        SCOPED_TRACE("direct copy unbound source");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *unboundSource,
            *copyDestination,
            [&](CommandList& commandList){
                commandList.copyTexture(
                    copyDestination.get(),
                    TextureSlice{},
                    unboundSource.get(),
                    TextureSlice{}
                );
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(unboundSource.get(), 0u, 0u));
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(copyDestination.get(), 0u, 0u));
            }
        );
    }
    {
        SCOPED_TRACE("staging readback unbound source");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *readback,
            *unboundSource,
            [&](CommandList& commandList){
                commandList.copyTexture(readback.get(), TextureSlice{}, unboundSource.get(), TextureSlice{});
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(unboundSource.get(), 0u, 0u));
            }
        );
    }
    {
        SCOPED_TRACE("staging upload unbound destination");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *upload,
            *unboundDestination,
            [&](CommandList& commandList){
                commandList.copyTexture(unboundDestination.get(), TextureSlice{}, upload.get(), TextureSlice{});
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(unboundDestination.get(), 0u, 0u));
            }
        );
    }
    {
        SCOPED_TRACE("staging readback image metadata mismatch");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *readback,
            *copySource,
            [&](CommandList& commandList){
                TextureDesc& mutableDesc = const_cast<TextureDesc&>(copySource->getDescription());
                const u32 width = mutableDesc.width;
                mutableDesc.width = width - 1u;
                commandList.copyTexture(readback.get(), TextureSlice{}, copySource.get(), TextureSlice{});
                mutableDesc.width = width;
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(copySource.get(), 0u, 0u));
            }
        );
    }
    {
        SCOPED_TRACE("staging upload image metadata mismatch");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *upload,
            *copyDestination,
            [&](CommandList& commandList){
                TextureDesc& mutableDesc = const_cast<TextureDesc&>(copyDestination->getDescription());
                const u32 width = mutableDesc.width;
                mutableDesc.width = width - 1u;
                commandList.copyTexture(copyDestination.get(), TextureSlice{}, upload.get(), TextureSlice{});
                mutableDesc.width = width;
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(copyDestination.get(), 0u, 0u));
            }
        );
    }

    u8 uploadBytes[16u * 16u * 4u] = {};
    {
        SCOPED_TRACE("write unbound destination");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *unboundDestination,
            *copySource,
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.tryWriteTexture(
                    unboundDestination.get(),
                    0u,
                    0u,
                    uploadBytes,
                    16u * 4u,
                    16u * 16u * 4u
                ));
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(unboundDestination.get(), 0u, 0u));
            }
        );
    }
}


TEST_F(TextureTransferAtomicityTest, ResolveRejectsUnboundAndForgedMetadataBeforeRangeWork){
    TextureDesc renderTargetDesc = ordinaryTextureDesc();
    renderTargetDesc.setInRenderTarget(true);
    TextureHandle renderTarget = device().createTexture(renderTargetDesc);
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(renderTarget.get())
    );

    TextureDesc sourceDesc = ordinaryTextureDesc();
    sourceDesc.setSampleCount(4u).setInRenderTarget(true);
    TextureHandle source = device().createTexture(sourceDesc);
    TextureHandle destination = device().createTexture(ordinaryTextureDesc());
    TextureHandle unboundDestination = createUnboundTexture();
    TextureDesc unboundSourceDesc = sourceDesc;
    unboundSourceDesc.isVirtual = true;
    TextureHandle unboundSource = device().createTexture(unboundSourceDesc);
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(framebuffer);
    if(!source || !destination)
        GTEST_SKIP() << "Texture-transfer resolve atomicity: RGBA8 4x multisampling is unavailable.";
    ASSERT_TRUE(unboundDestination);
    ASSERT_TRUE(unboundSource);
    ASSERT_FALSE(device().isTextureReadyForGpuUse(unboundSource.get()));

    {
        SCOPED_TRACE("resolve unbound destination");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *source,
            *unboundDestination,
            [&](CommandList& commandList){
                commandList.resolveTexture(
                    unboundDestination.get(),
                    s_AllSubresources,
                    source.get(),
                    s_AllSubresources
                );
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(source.get(), 0u, 0u));
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(unboundDestination.get(), 0u, 0u));
            }
        );
    }
    {
        SCOPED_TRACE("resolve unbound multisampled source");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *unboundSource,
            *destination,
            [&](CommandList& commandList){
                commandList.resolveTexture(
                    destination.get(),
                    s_AllSubresources,
                    unboundSource.get(),
                    s_AllSubresources
                );
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(unboundSource.get(), 0u, 0u));
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(destination.get(), 0u, 0u));
            }
        );
    }
    {
        SCOPED_TRACE("resolve forged huge mip metadata");
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *source,
            *destination,
            [&](CommandList& commandList){
                TextureDesc& mutableSourceDesc = const_cast<TextureDesc&>(source->getDescription());
                TextureDesc& mutableDestinationDesc = const_cast<TextureDesc&>(destination->getDescription());
                const MipLevel sourceMipLevels = mutableSourceDesc.mipLevels;
                const MipLevel destinationMipLevels = mutableDestinationDesc.mipLevels;
                mutableSourceDesc.mipLevels = Limit<MipLevel>::s_Max;
                mutableDestinationDesc.mipLevels = Limit<MipLevel>::s_Max;
                const TextureSubresourceSet forgedRange(0u, Limit<MipLevel>::s_Max, 0u, 1u);
                commandList.resolveTexture(destination.get(), forgedRange, source.get(), forgedRange);
                mutableSourceDesc.mipLevels = sourceMipLevels;
                mutableDestinationDesc.mipLevels = destinationMipLevels;
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(source.get(), 0u, 0u));
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(destination.get(), 0u, 0u));
            }
        );
    }
}


TEST_F(TextureTransferAtomicityTest, WriteRejectsUnsafeInputsInEveryConfiguration){
    TextureDesc renderTargetDesc = ordinaryTextureDesc();
    renderTargetDesc.setInRenderTarget(true);
    TextureHandle renderTarget = device().createTexture(renderTargetDesc);
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(renderTarget.get())
    );
    const TextureDesc destinationDesc = ordinaryTextureDesc();
    TextureHandle destination = device().createTexture(destinationDesc);
    TextureHandle auxiliaryTexture = device().createTexture(ordinaryTextureDesc());
    TextureDesc multisampleDesc = ordinaryTextureDesc();
    multisampleDesc.setSampleCount(4u).setInRenderTarget(true);
    TextureHandle multisampleDestination = device().createTexture(multisampleDesc);
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(framebuffer);
    ASSERT_TRUE(destination);
    ASSERT_TRUE(auxiliaryTexture);

    u8 uploadBytes[16u * 16u * 4u] = {};
    const auto expectWriteRejection = [&](const char* const label, Texture* const observedTexture, auto&& operation){
        SCOPED_TRACE(label);
        __hidden_texture_transfer_atomicity_tests::ExpectAtomicTransferRejection(
            device(),
            *framebuffer,
            *renderTarget,
            *observedTexture,
            *auxiliaryTexture,
            [&](CommandList& commandList){
                operation(commandList);
            },
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.hasExplicitTextureSubresourceState(observedTexture, 0u, 0u));
            }
        );
    };

    expectWriteRejection("null destination", destination.get(), [&](CommandList& commandList){
        EXPECT_FALSE(commandList.tryWriteTexture(nullptr, 0u, 0u, uploadBytes, 16u * 4u, 16u * 16u * 4u));
    });
    expectWriteRejection("null data", destination.get(), [&](CommandList& commandList){
        EXPECT_FALSE(commandList.tryWriteTexture(destination.get(), 0u, 0u, nullptr, 16u * 4u, 16u * 16u * 4u));
    });
    expectWriteRejection("out-of-bounds mip", destination.get(), [&](CommandList& commandList){
        EXPECT_FALSE(commandList.tryWriteTexture(
            destination.get(),
            0u,
            destinationDesc.mipLevels,
            uploadBytes,
            16u * 4u,
            16u * 16u * 4u
        ));
    });
    expectWriteRejection("out-of-bounds array slice", destination.get(), [&](CommandList& commandList){
        EXPECT_FALSE(commandList.tryWriteTexture(
            destination.get(),
            destinationDesc.arraySize,
            0u,
            uploadBytes,
            16u * 4u,
            16u * 16u * 4u
        ));
    });
    expectWriteRejection("undersized row pitch", destination.get(), [&](CommandList& commandList){
        EXPECT_FALSE(commandList.tryWriteTexture(
            destination.get(),
            0u,
            0u,
            uploadBytes,
            16u * 4u - 4u,
            16u * 16u * 4u
        ));
    });
    expectWriteRejection("undersized depth pitch", destination.get(), [&](CommandList& commandList){
        EXPECT_FALSE(commandList.tryWriteTexture(
            destination.get(),
            0u,
            0u,
            uploadBytes,
            16u * 4u,
            15u * 16u * 4u
        ));
    });
    if(multisampleDestination){
        expectWriteRejection(
            "multisampled destination",
            multisampleDestination.get(),
            [&](CommandList& commandList){
                EXPECT_FALSE(commandList.tryWriteTexture(
                    multisampleDestination.get(),
                    0u,
                    0u,
                    uploadBytes,
                    16u * 4u,
                    16u * 16u * 4u
                ));
            }
        );
    }
    expectWriteRejection("invalid format metadata", destination.get(), [&](CommandList& commandList){
        TextureDesc& mutableDesc = const_cast<TextureDesc&>(destination->getDescription());
        const Format::Enum format = mutableDesc.format;
        mutableDesc.format = static_cast<Format::Enum>(Limit<u8>::s_Max);
        const bool accepted = commandList.tryWriteTexture(
            destination.get(),
            0u,
            0u,
            uploadBytes,
            16u * 4u,
            16u * 16u * 4u
        );
        mutableDesc.format = format;
        EXPECT_FALSE(accepted);
    });

    CommandListHandle recovery = device().createCommandList();
    ASSERT_TRUE(recovery);
    recovery->open();
    recovery->close();
    ASSERT_TRUE(recovery->hasCommandBuffer());
    recovery->open();
    ASSERT_TRUE(recovery->tryWriteTexture(
        destination.get(),
        0u,
        0u,
        uploadBytes,
        16u * 4u,
        16u * 16u * 4u
    ));
    EXPECT_FALSE(recovery->commandRecordingFailed());
    recovery->close();
    ASSERT_TRUE(recovery->hasCommandBuffer());
    CommandList* const recoveryLists[] = { recovery.get() };
    const QueueSubmissionToken recoveryToken = device().executeCommandLists(
        recoveryLists,
        LengthOf(recoveryLists),
        recovery->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(recoveryToken.valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(TextureTransferAtomicityTest, PartialStagingCopiesUseGraphicsOrComputeCapability){
    TextureDesc desc = ordinaryTextureDesc();
    desc.setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer);
    TextureHandle uploadDestination = device().createTexture(desc);
    TextureHandle readbackSource = device().createTexture(desc);
    StagingTextureHandle upload = device().createStagingTexture(desc, CpuAccessMode::Write);
    StagingTextureHandle readback = device().createStagingTexture(desc, CpuAccessMode::Read);
    ASSERT_TRUE(uploadDestination);
    ASSERT_TRUE(readbackSource);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(readback);

    const TextureSlice partialSlice = TextureSlice().setSize(4u, 4u, 1u);
    CommandListHandle graphicsList = device().createCommandList();
    ASSERT_TRUE(graphicsList);
    graphicsList->open();
    graphicsList->copyTexture(uploadDestination.get(), partialSlice, upload.get(), partialSlice);
    graphicsList->copyTexture(readback.get(), partialSlice, readbackSource.get(), partialSlice);
    EXPECT_FALSE(graphicsList->commandRecordingFailed());
    graphicsList->close();
    ASSERT_TRUE(graphicsList->hasCommandBuffer());
    CommandList* const graphicsLists[] = { graphicsList.get() };
    const QueueSubmissionToken graphicsToken = device().executeCommandLists(
        graphicsLists,
        LengthOf(graphicsLists),
        graphicsList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsToken.valid());
    ASSERT_TRUE(device().waitForIdle());

    const GpuPhysicalQueueTopology topology = device().getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* transferOnlyQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        const u8 capabilities = static_cast<u8>(candidate.capabilities);
        if(
            (capabilities & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u
            && (capabilities & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
            && (capabilities & static_cast<u8>(GpuQueueCapability::Compute)) == 0u
        ){
            transferOnlyQueue = &candidate;
            break;
        }
    }
    if(transferOnlyQueue){
        TextureHandle transferDestination = device().createTexture(desc);
        ASSERT_TRUE(transferDestination);
        CommandListParameters transferParameters;
        transferParameters.setPhysicalQueue(transferOnlyQueue->id);
        CommandListHandle transferList = device().createCommandList(transferParameters);
        ASSERT_TRUE(transferList);
        const u32 stagingReferences = upload->getReferenceCount();
        const u32 imageReferences = transferDestination->getReferenceCount();

        transferList->open();
        transferList->copyTexture(transferDestination.get(), partialSlice, upload.get(), partialSlice);
        EXPECT_TRUE(transferList->commandRecordingFailed());
        EXPECT_FALSE(transferList->hasExplicitTextureSubresourceState(transferDestination.get(), 0u, 0u));
        EXPECT_EQ(upload->getReferenceCount(), stagingReferences);
        EXPECT_EQ(transferDestination->getReferenceCount(), imageReferences);
        transferList->close();
        EXPECT_FALSE(transferList->hasCommandBuffer());
    }
}


TEST_F(TextureTransferAtomicityTest, SequentialWritesHonorFormatBlockOffsetAlignment){
    if((device().queryFormatSupport(Format::RGB32_FLOAT) & FormatSupport::Texture) != FormatSupport::Texture)
        GTEST_SKIP() << "Texture upload alignment: RGB32_FLOAT textures are unsupported.";

    const TextureDesc rgbaDesc = TextureDesc()
        .setWidth(1u)
        .setHeight(1u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureDesc rgbDesc = rgbaDesc;
    rgbDesc.setFormat(Format::RGB32_FLOAT);
    TextureHandle rgbaDestination = device().createTexture(rgbaDesc);
    TextureHandle rgbDestination = device().createTexture(rgbDesc);
    ASSERT_TRUE(rgbaDestination);
    ASSERT_TRUE(rgbDestination);

    const u8 rgbaBytes[4u] = {};
    const u8 rgbBytes[12u] = {};
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(commandList->tryWriteTexture(rgbaDestination.get(), 0u, 0u, rgbaBytes, 4u, 4u));
    ASSERT_TRUE(commandList->tryWriteTexture(rgbDestination.get(), 0u, 0u, rgbBytes, 12u, 12u));
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        commandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(token.valid());
    EXPECT_TRUE(device().waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

